#pragma once

#include <compare>
#include <cstdint>

namespace cbor::tags {

using positive = std::uint64_t;
struct integer;

struct negative {
    positive value; // Magnitude; zero represents the CBOR-only value -2^64.

    constexpr negative() : value(0) {}
    constexpr negative(positive N) : value(N) {}

    constexpr std::strong_ordering operator<=>(const negative &rhs) const noexcept {
        if (value == rhs.value) {
            return std::strong_ordering::equal;
        }
        if (value == 0) {
            return std::strong_ordering::less;
        }
        if (rhs.value == 0) {
            return std::strong_ordering::greater;
        }
        return rhs.value <=> value; // Reverse comparison because negative numbers
    }

    constexpr bool operator==(const negative &rhs) const noexcept { return value == rhs.value; }
};

struct integer {
    positive value; // For a negative value, zero represents -2^64.
    bool     is_negative;

    constexpr integer(positive N) : value(N), is_negative(false) {}
    constexpr integer(positive N, bool is_negative) : value(N), is_negative(is_negative) {}
    constexpr integer(negative N) : value(N.value), is_negative(true) {}

    constexpr std::strong_ordering operator<=>(const integer &rhs) const noexcept {
        if (is_negative && rhs.is_negative) {
            return negative{value} <=> negative{rhs.value};
        } else if (is_negative) {
            return std::strong_ordering::less;
        } else if (rhs.is_negative) {
            return std::strong_ordering::greater;
        } else {
            return value <=> rhs.value;
        }
    }

    constexpr bool operator==(const integer &rhs) const noexcept { return value == rhs.value && is_negative == rhs.is_negative; }
};

constexpr auto operator""_neg(unsigned long long N) { return negative(N); }

} // namespace cbor::tags
