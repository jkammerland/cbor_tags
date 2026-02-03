#pragma once

#include "someip/ser/detail.h"

#include <cstring>

namespace someip::ser::detail {

template <class T>
expected<void> decode_impl(wire::reader &in, const config &cfg, T &out, std::size_t base_offset) noexcept;

template <class T>
expected<void> decode_scalar(wire::reader &in, const config &cfg, T &out) noexcept {
    if constexpr (is_byte<T>) {
        (void)cfg;
        auto b = in.read_byte();
        if (!b) return unexpected<status_code>(b.error());
        out = *b;
        return {};
    } else if constexpr (is_bool<T>) {
        (void)cfg;
        auto b = in.read_byte();
        if (!b) return unexpected<status_code>(b.error());
        const auto u = std::to_integer<std::uint8_t>(*b);
        if ((u & 0xFEu) != 0u) {
            return unexpected<status_code>(status_code::invalid_bool_value);
        }
        out = (u & 0x01u) != 0u;
        return {};
    } else if constexpr (is_integral_not_bool<T> && std::is_unsigned_v<std::remove_cvref_t<T>>) {
        using U = std::make_unsigned_t<std::remove_cvref_t<T>>;
        auto v  = wire::read_uint<U>(cfg.payload_endian, in);
        if (!v) return unexpected<status_code>(v.error());
        out = static_cast<std::remove_cvref_t<T>>(*v);
        return {};
    } else if constexpr (is_integral_not_bool<T> && std::is_signed_v<std::remove_cvref_t<T>>) {
        using S = std::remove_cvref_t<T>;
        using U = std::make_unsigned_t<S>;
        auto v  = wire::read_uint<U>(cfg.payload_endian, in);
        if (!v) return unexpected<status_code>(v.error());
        out = std::bit_cast<S>(static_cast<U>(*v));
        return {};
    } else if constexpr (is_enum<T>) {
        using U = std::underlying_type_t<std::remove_cvref_t<T>>;
        U       tmp{};
        auto    st = decode_scalar(in, cfg, tmp);
        if (!st) return st;
        out = static_cast<std::remove_cvref_t<T>>(tmp);
        return {};
    } else if constexpr (is_float32<T>) {
        auto v = wire::read_uint<std::uint32_t>(cfg.payload_endian, in);
        if (!v) return unexpected<status_code>(v.error());
        out = std::bit_cast<float>(*v);
        return {};
    } else if constexpr (is_float64<T>) {
        auto v = wire::read_uint<std::uint64_t>(cfg.payload_endian, in);
        if (!v) return unexpected<status_code>(v.error());
        out = std::bit_cast<double>(*v);
        return {};
    } else {
        (void)in;
        (void)cfg;
        (void)out;
        return unexpected<status_code>(status_code::error);
    }
}

template <std::size_t N>
expected<void> decode_impl(wire::reader &in, const config &cfg, std::array<std::byte, N> &out, std::size_t base_offset) noexcept {
    (void)cfg;
    (void)base_offset;
    auto view = in.read_bytes(N);
    if (!view) return unexpected<status_code>(view.error());
    std::memcpy(out.data(), view->data(), N);
    return {};
}

template <class T, std::size_t N>
expected<void> decode_impl(wire::reader &in, const config &cfg, std::array<T, N> &out, std::size_t base_offset) noexcept {
    for (auto &e : out) {
        auto st = decode_impl(in, cfg, e, base_offset);
        if (!st) return st;
    }
    return {};
}

template <std::size_t N>
expected<void> decode_impl(wire::reader &in, const config &cfg, types::pad_bytes<N> &, std::size_t base_offset) noexcept {
    (void)cfg;
    (void)base_offset;
    return in.skip(N);
}

template <std::size_t AlignBits>
expected<void> decode_impl(wire::reader &in, const config &cfg, types::pad_to<AlignBits> &, std::size_t base_offset) noexcept {
    (void)cfg;
    return decode_pad_to<AlignBits>(in, base_offset);
}

template <int LenBits, int AlignAfterBits>
expected<void> decode_impl(wire::reader &in, const config &cfg, types::utf8_string<LenBits, AlignAfterBits> &out, std::size_t base_offset) noexcept {
    (void)cfg;
    auto len = read_len_field<LenBits>(in);
    if (!len) return unexpected<status_code>(len.error());
    if (*len < 4u) {
        return unexpected<status_code>(status_code::invalid_length);
    }
    auto payload = in.read_bytes(*len);
    if (!payload) return unexpected<status_code>(payload.error());

    const auto bytes = *payload;
    if (bytes.size() < 4) {
        return unexpected<status_code>(status_code::invalid_length);
    }

    // BOM EF BB BF
    if (std::to_integer<std::uint8_t>(bytes[0]) != 0xEFu || std::to_integer<std::uint8_t>(bytes[1]) != 0xBBu ||
        std::to_integer<std::uint8_t>(bytes[2]) != 0xBFu) {
        return unexpected<status_code>(status_code::invalid_bom);
    }
    if (bytes.back() != std::byte{0x00}) {
        return unexpected<status_code>(status_code::invalid_string_termination);
    }

    const auto text = std::string_view(reinterpret_cast<const char *>(bytes.data() + 3), bytes.size() - 4);
    if (!is_valid_utf8(text)) {
        return unexpected<status_code>(status_code::invalid_utf8);
    }
    out.value.assign(text.data(), text.size());

    if constexpr (AlignAfterBits != 0) {
        auto st = decode_pad_to<static_cast<std::size_t>(AlignAfterBits)>(in, base_offset);
        if (!st) return st;
    }
    return {};
}

template <int LenBits, int AlignAfterBits>
expected<void> decode_impl(wire::reader &in, const config &cfg, types::utf16_string<LenBits, AlignAfterBits> &out, std::size_t base_offset) noexcept {
    auto len = read_len_field<LenBits>(in);
    if (!len) return unexpected<status_code>(len.error());
    if (*len < 4u || (*len % 2u) != 0u) {
        return unexpected<status_code>(status_code::invalid_utf16);
    }
    auto payload = in.read_bytes(*len);
    if (!payload) return unexpected<status_code>(payload.error());

    const auto bytes = *payload;
    if (bytes.size() < 4) {
        return unexpected<status_code>(status_code::invalid_utf16);
    }

    // Terminator
    if (bytes[bytes.size() - 2] != std::byte{0x00} || bytes[bytes.size() - 1] != std::byte{0x00}) {
        return unexpected<status_code>(status_code::invalid_string_termination);
    }

    const bool little = (cfg.payload_endian == wire::endian::little);
    const std::uint8_t bom0 = little ? 0xFFu : 0xFEu;
    const std::uint8_t bom1 = little ? 0xFEu : 0xFFu;
    if (std::to_integer<std::uint8_t>(bytes[0]) != bom0 || std::to_integer<std::uint8_t>(bytes[1]) != bom1) {
        return unexpected<status_code>(status_code::invalid_bom);
    }

    const auto data_len = bytes.size() - 4; // exclude BOM(2) + terminator(2)
    if ((data_len % 2u) != 0u) {
        return unexpected<status_code>(status_code::invalid_utf16);
    }

    out.value.clear();
    out.value.reserve(data_len / 2u);

    wire::reader tmp{std::span<const std::byte>(bytes.data() + 2, data_len)};
    for (std::size_t i = 0; i < data_len / 2u; ++i) {
        auto cu = wire::read_uint<std::uint16_t>(cfg.payload_endian, tmp);
        if (!cu) return unexpected<status_code>(cu.error());
        out.value.push_back(static_cast<char16_t>(*cu));
    }

    if constexpr (AlignAfterBits != 0) {
        auto st = decode_pad_to<static_cast<std::size_t>(AlignAfterBits)>(in, base_offset);
        if (!st) return st;
    }
    return {};
}

template <class T, int LenBits, int AlignAfterBits>
expected<void> decode_impl(wire::reader &in, const config &cfg, types::dyn_array<T, LenBits, AlignAfterBits> &out, std::size_t base_offset) noexcept {
    static_assert(is_scalar<T>, "dyn_array only supports scalar element types in this POC");

    auto len = read_len_field<LenBits>(in);
    if (!len) return unexpected<status_code>(len.error());

    constexpr std::size_t elem_size = sizeof(std::remove_cvref_t<T>);
    if (elem_size == 0 || (*len % elem_size) != 0u) {
        return unexpected<status_code>(status_code::invalid_length);
    }

    const auto count = static_cast<std::size_t>(*len / elem_size);
    out.value.clear();
    out.value.resize(count);

    for (auto &e : out.value) {
        auto st = decode_impl(in, cfg, e, base_offset);
        if (!st) return st;
    }

    if constexpr (AlignAfterBits != 0) {
        auto st = decode_pad_to<static_cast<std::size_t>(AlignAfterBits)>(in, base_offset);
        if (!st) return st;
    }

    return {};
}

template <class T, std::size_t N, int OptionalLenBits>
expected<void> decode_impl(wire::reader &in, const config &cfg, types::fixed_array<T, N, OptionalLenBits> &out, std::size_t base_offset) noexcept {
    static_assert(OptionalLenBits == 0 || is_scalar<T>, "fixed_array optional length only supported for scalar types in this POC");

    if constexpr (OptionalLenBits != 0) {
        auto len = read_len_field<OptionalLenBits>(in);
        if (!len) return unexpected<status_code>(len.error());
        const auto expected_bytes = static_cast<std::uint32_t>(N * sizeof(std::remove_cvref_t<T>));
        if (*len != expected_bytes) {
            return unexpected<status_code>(status_code::invalid_length);
        }
    }

    for (auto &e : out.value) {
        auto st = decode_impl(in, cfg, e, base_offset);
        if (!st) return st;
    }
    return {};
}

template <std::size_t I, class Variant>
expected<void> decode_variant_alt(std::size_t idx, wire::reader &in, const config &cfg, Variant &out, std::size_t base_offset) noexcept {
    if constexpr (I >= std::variant_size_v<Variant>) {
        (void)idx;
        (void)in;
        (void)cfg;
        (void)out;
        (void)base_offset;
        return unexpected<status_code>(status_code::invalid_union_selector);
    } else {
        if (idx == I) {
            using Alt = std::variant_alternative_t<I, Variant>;
            Alt tmp{};
            auto st = decode_impl(in, cfg, tmp, base_offset);
            if (!st) return st;
            out = std::move(tmp);
            return {};
        }
        return decode_variant_alt<I + 1>(idx, in, cfg, out, base_offset);
    }
}

template <class Variant, int LenBits, int SelectorBits, int AlignPayloadBits>
expected<void> decode_impl(wire::reader &in, const config &cfg, types::union_variant<Variant, LenBits, SelectorBits, AlignPayloadBits> &out,
                           std::size_t base_offset) noexcept {
    static_assert(std::variant_size_v<Variant> >= 1, "Variant must not be empty");
    static_assert(std::is_same_v<std::variant_alternative_t<0, Variant>, std::monostate>, "Variant[0] must be std::monostate");
    static_assert(LenBits == 8 || LenBits == 16 || LenBits == 32, "LenBits must be 8, 16, or 32");
    static_assert(SelectorBits == 8 || SelectorBits == 16 || SelectorBits == 32, "SelectorBits must be 8, 16, or 32");
    static_assert(AlignPayloadBits == 0 || (AlignPayloadBits % 8 == 0), "AlignPayloadBits must be 0 or a multiple of 8");

    auto len = read_len_field<LenBits>(in);
    if (!len) return unexpected<status_code>(len.error());
    auto sel = read_len_field<SelectorBits>(in);
    if (!sel) return unexpected<status_code>(sel.error());

    const auto region_start = in.position();
    if (in.remaining() < *len) {
        return unexpected<status_code>(status_code::buffer_overrun);
    }
    const auto region_end = region_start + static_cast<std::size_t>(*len);

    if (*sel == 0u) {
        out.value = std::monostate{};
        auto st = in.skip(*len);
        if (!st) return st;
        return {};
    }

    if (*sel >= std::variant_size_v<Variant>) {
        return unexpected<status_code>(status_code::invalid_union_selector);
    }

    auto st = decode_variant_alt<1>(*sel, in, cfg, out.value, base_offset);
    if (!st) return st;

    if (in.position() > region_end) {
        return unexpected<status_code>(status_code::invalid_length);
    }
    auto remaining = region_end - in.position();
    if (remaining > 0) {
        st = in.skip(remaining);
        if (!st) return st;
    }

    return {};
}

template <class T>
expected<void> decode_impl(wire::reader &in, const config &cfg, T &out, std::size_t base_offset) noexcept {
    if constexpr (is_scalar<T>) {
        return decode_scalar(in, cfg, out);
    } else if constexpr (std::is_aggregate_v<std::remove_cvref_t<T>> && !is_someip_wrapper<std::remove_cvref_t<T>>) {
        auto tuple = cbor::tags::to_tuple(out);
        return std::apply(
            [&](auto &...args) -> expected<void> {
                expected<void> st{};
                auto           one = [&](auto &arg) {
                    if (!st) {
                        return;
                    }
                    st = decode_impl(in, cfg, arg, base_offset);
                };
                (one(args), ...);
                return st;
            },
            tuple);
    } else {
        (void)in;
        (void)cfg;
        (void)out;
        (void)base_offset;
        return unexpected<status_code>(status_code::error);
    }
}

} // namespace someip::ser::detail

namespace someip::ser {

template <class T>
expected<void> decode(std::span<const std::byte> in, const config &cfg, T &out, std::size_t base_offset = 0) noexcept {
    wire::reader r{in};
    auto         st = detail::decode_impl(r, cfg, out, base_offset);
    if (!st) {
        return unexpected<status_code>(st.error());
    }
    if (!r.empty()) {
        return unexpected<status_code>(status_code::invalid_length);
    }
    return {};
}

} // namespace someip::ser

