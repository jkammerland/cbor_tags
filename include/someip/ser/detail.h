#pragma once

#include "cbor_tags/cbor_reflection.h"
#include "someip/ser/config.h"
#include "someip/status.h"
#include "someip/types/array.h"
#include "someip/types/padding.h"
#include "someip/types/string.h"
#include "someip/types/union.h"
#include "someip/wire/cursor.h"
#include "someip/wire/endian.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>

namespace someip::ser::detail {

template <class T>
concept is_someip_padding = requires { typename T::someip_padding_tag; };

template <class T>
concept is_someip_string = requires { typename T::someip_string_tag; };

template <class T>
concept is_someip_array = requires { typename T::someip_array_tag; };

template <class T>
concept is_someip_union = requires { typename T::someip_union_tag; };

template <class T>
concept is_someip_wrapper = is_someip_padding<T> || is_someip_string<T> || is_someip_array<T> || is_someip_union<T>;

template <class T>
concept is_byte = std::is_same_v<std::remove_cvref_t<T>, std::byte>;

template <class T>
concept is_bool = std::is_same_v<std::remove_cvref_t<T>, bool>;

template <class T>
concept is_integral_not_bool = std::is_integral_v<std::remove_cvref_t<T>> && !is_bool<T>;

template <class T>
concept is_enum = std::is_enum_v<std::remove_cvref_t<T>>;

template <class T>
concept is_float32 = std::is_same_v<std::remove_cvref_t<T>, float>;

template <class T>
concept is_float64 = std::is_same_v<std::remove_cvref_t<T>, double>;

template <class T>
concept is_scalar = is_byte<T> || is_bool<T> || is_integral_not_bool<T> || is_enum<T> || is_float32<T> || is_float64<T>;

template <int Bits>
constexpr std::size_t bytes_for_bits() {
    static_assert(Bits == 8 || Bits == 16 || Bits == 32, "Bits must be 8, 16, or 32");
    return static_cast<std::size_t>(Bits / 8);
}

inline constexpr std::size_t pad_needed(std::size_t offset, std::size_t align) noexcept {
    if (align == 0) {
        return 0;
    }
    const auto rem = offset % align;
    return rem == 0 ? 0 : (align - rem);
}

template <class Out>
expected<void> write_pad_bytes(wire::writer<Out> &out, std::size_t n, std::byte pad_byte) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        auto st = out.write_byte(pad_byte);
        if (!st) {
            return st;
        }
    }
    return {};
}

inline bool is_valid_utf8(std::string_view s) noexcept {
    const auto *p   = reinterpret_cast<const unsigned char *>(s.data());
    const auto  len = s.size();
    std::size_t i   = 0;

    while (i < len) {
        const auto c0 = p[i];
        if (c0 <= 0x7F) {
            ++i;
            continue;
        }

        auto cont = [&](std::size_t j) -> bool { return j < len && (p[j] & 0xC0u) == 0x80u; };

        if ((c0 & 0xE0u) == 0xC0u) {
            if (!cont(i + 1)) return false;
            const std::uint32_t cp = (std::uint32_t(c0 & 0x1Fu) << 6) | std::uint32_t(p[i + 1] & 0x3Fu);
            if (cp < 0x80u) return false; // overlong
            i += 2;
        } else if ((c0 & 0xF0u) == 0xE0u) {
            if (!cont(i + 1) || !cont(i + 2)) return false;
            const std::uint32_t cp = (std::uint32_t(c0 & 0x0Fu) << 12) | (std::uint32_t(p[i + 1] & 0x3Fu) << 6) |
                                     std::uint32_t(p[i + 2] & 0x3Fu);
            if (cp < 0x800u) return false; // overlong
            if (cp >= 0xD800u && cp <= 0xDFFFu) return false; // surrogate range invalid in UTF-8
            i += 3;
        } else if ((c0 & 0xF8u) == 0xF0u) {
            if (!cont(i + 1) || !cont(i + 2) || !cont(i + 3)) return false;
            const std::uint32_t cp = (std::uint32_t(c0 & 0x07u) << 18) | (std::uint32_t(p[i + 1] & 0x3Fu) << 12) |
                                     (std::uint32_t(p[i + 2] & 0x3Fu) << 6) | std::uint32_t(p[i + 3] & 0x3Fu);
            if (cp < 0x10000u) return false; // overlong
            if (cp > 0x10FFFFu) return false;
            i += 4;
        } else {
            return false;
        }
    }

    return true;
}

struct sizer {
    std::size_t pos{0};
    void        advance(std::size_t n) noexcept { pos += n; }
    [[nodiscard]] std::size_t position() const noexcept { return pos; }
};

template <int Bits, class Out>
expected<void> write_len_field(wire::writer<Out> &out, std::uint32_t v) noexcept {
    if constexpr (Bits == 8) {
        if (v > std::numeric_limits<std::uint8_t>::max()) return unexpected<status_code>(status_code::invalid_length);
        return wire::write_uint<wire::endian::big>(out, static_cast<std::uint8_t>(v));
    } else if constexpr (Bits == 16) {
        if (v > std::numeric_limits<std::uint16_t>::max()) return unexpected<status_code>(status_code::invalid_length);
        return wire::write_uint<wire::endian::big>(out, static_cast<std::uint16_t>(v));
    } else if constexpr (Bits == 32) {
        return wire::write_uint<wire::endian::big>(out, static_cast<std::uint32_t>(v));
    } else {
        static_assert(Bits == 8 || Bits == 16 || Bits == 32, "Bits must be 8, 16, or 32");
        return unexpected<status_code>(status_code::error);
    }
}

template <int Bits>
expected<std::uint32_t> read_len_field(wire::reader &in) noexcept {
    if constexpr (Bits == 8) {
        auto v = wire::read_uint<wire::endian::big, std::uint8_t>(in);
        if (!v) return unexpected<status_code>(v.error());
        return static_cast<std::uint32_t>(*v);
    } else if constexpr (Bits == 16) {
        auto v = wire::read_uint<wire::endian::big, std::uint16_t>(in);
        if (!v) return unexpected<status_code>(v.error());
        return static_cast<std::uint32_t>(*v);
    } else if constexpr (Bits == 32) {
        auto v = wire::read_uint<wire::endian::big, std::uint32_t>(in);
        if (!v) return unexpected<status_code>(v.error());
        return *v;
    } else {
        static_assert(Bits == 8 || Bits == 16 || Bits == 32, "Bits must be 8, 16, or 32");
        return unexpected<status_code>(status_code::error);
    }
}

template <std::size_t AlignBits, class Out>
expected<void> encode_pad_to(wire::writer<Out> &out, const config &cfg, std::size_t base_offset) noexcept {
    static_assert(AlignBits % 8u == 0u, "AlignBits must be a multiple of 8");
    static_assert(AlignBits > 0u, "AlignBits must be > 0");
    const auto align = AlignBits / 8u;
    const auto off   = base_offset + out.position();
    const auto pad   = pad_needed(off, align);
    return write_pad_bytes(out, pad, cfg.pad_byte);
}

template <std::size_t AlignBits>
expected<void> decode_pad_to(wire::reader &in, std::size_t base_offset) noexcept {
    static_assert(AlignBits % 8u == 0u, "AlignBits must be a multiple of 8");
    static_assert(AlignBits > 0u, "AlignBits must be > 0");
    const auto align = AlignBits / 8u;
    const auto off   = base_offset + in.position();
    const auto pad   = pad_needed(off, align);
    return in.skip(pad);
}

} // namespace someip::ser::detail
