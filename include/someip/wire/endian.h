#pragma once

#include "someip/status.h"
#include "someip/wire/cursor.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace someip::wire {

enum class endian : std::uint8_t { big = 0, little = 1 };

namespace detail {

template <class UInt>
using require_uint = std::enable_if_t<std::is_unsigned_v<UInt> && std::is_integral_v<UInt>, int>;

} // namespace detail

template <endian E, class UInt, class Out, detail::require_uint<UInt> = 0>
expected<void> write_uint(writer<Out> &out, UInt v) noexcept {
    constexpr std::size_t n = sizeof(UInt);
    if constexpr (E == endian::big) {
        for (std::size_t i = 0; i < n; ++i) {
            auto st = out.write_byte(std::byte((v >> ((n - 1 - i) * 8)) & 0xFFu));
            if (!st) {
                return st;
            }
        }
    } else {
        for (std::size_t i = 0; i < n; ++i) {
            auto st = out.write_byte(std::byte((v >> (i * 8)) & 0xFFu));
            if (!st) {
                return st;
            }
        }
    }
    return {};
}

template <class UInt, class Out, detail::require_uint<UInt> = 0>
expected<void> write_uint(endian e, writer<Out> &out, UInt v) noexcept {
    if (e == endian::big) {
        return write_uint<endian::big>(out, v);
    }
    return write_uint<endian::little>(out, v);
}

template <endian E, class UInt, detail::require_uint<UInt> = 0>
expected<UInt> read_uint(reader &in) noexcept {
    constexpr std::size_t n = sizeof(UInt);
    if (in.remaining() < n) {
        return unexpected<status_code>(status_code::buffer_overrun);
    }
    UInt v{0};
    if constexpr (E == endian::big) {
        for (std::size_t i = 0; i < n; ++i) {
            const auto b = in.read_byte();
            if (!b) {
                return unexpected<status_code>(b.error());
            }
            v = static_cast<UInt>((v << 8) | std::to_integer<std::uint8_t>(*b));
        }
    } else {
        for (std::size_t i = 0; i < n; ++i) {
            const auto b = in.read_byte();
            if (!b) {
                return unexpected<status_code>(b.error());
            }
            v |= static_cast<UInt>(static_cast<UInt>(std::to_integer<std::uint8_t>(*b)) << (i * 8));
        }
    }
    return v;
}

template <class UInt, detail::require_uint<UInt> = 0>
expected<UInt> read_uint(endian e, reader &in) noexcept {
    if (e == endian::big) {
        return read_uint<endian::big, UInt>(in);
    }
    return read_uint<endian::little, UInt>(in);
}

template <class Out>
expected<void> write_u24_be(writer<Out> &out, std::uint32_t v) noexcept {
    if (v > 0xFFFFFFu) {
        return unexpected<status_code>(status_code::invalid_length);
    }
    auto st0 = out.write_byte(std::byte((v >> 16) & 0xFFu));
    if (!st0) return st0;
    auto st1 = out.write_byte(std::byte((v >> 8) & 0xFFu));
    if (!st1) return st1;
    auto st2 = out.write_byte(std::byte((v >> 0) & 0xFFu));
    if (!st2) return st2;
    return {};
}

inline expected<std::uint32_t> read_u24_be(reader &in) noexcept {
    if (in.remaining() < 3) {
        return unexpected<status_code>(status_code::buffer_overrun);
    }
    const auto b0 = in.read_byte();
    if (!b0) return unexpected<status_code>(b0.error());
    const auto b1 = in.read_byte();
    if (!b1) return unexpected<status_code>(b1.error());
    const auto b2 = in.read_byte();
    if (!b2) return unexpected<status_code>(b2.error());
    return (std::uint32_t(std::to_integer<std::uint8_t>(*b0)) << 16) | (std::uint32_t(std::to_integer<std::uint8_t>(*b1)) << 8) |
           std::uint32_t(std::to_integer<std::uint8_t>(*b2));
}

} // namespace someip::wire
