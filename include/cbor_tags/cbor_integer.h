#pragma once

#include <compare>
#include <cstdint>
#include <limits>

namespace cbor::tags {

using positive = std::uint64_t;
struct integer;

struct negative {
    positive value;

    constexpr negative() : value(0) {}
    constexpr negative(positive N) : value(N) {}

    constexpr auto operator<=>(const negative &rhs) const {
        return rhs.value <=> value; // Reverse comparison because negative numbers
    }

    constexpr auto operator==(const negative &rhs) const { return value == rhs.value; }

    // Unary operators
    constexpr std::uint64_t operator-() const { return value; }
    constexpr negative      operator+() const { return *this; }
};

struct integer {
    positive value;
    bool     is_negative;

    constexpr integer(positive N) : value(N), is_negative(false) {}
    constexpr integer(positive N, bool is_negative) : value(N), is_negative(is_negative) {}
    constexpr integer(negative N) : value(N.value), is_negative(true) {}

    constexpr auto operator<=>(const integer &rhs) const {
        if (is_negative && rhs.is_negative) {
            return rhs.value <=> value; // Reverse comparison because negative numbers
        } else if (is_negative) {
            return -1 <=> 0;
        } else if (rhs.is_negative) {
            return 1 <=> 0;
        } else {
            return value <=> rhs.value;
        }
    }

    constexpr auto operator==(const integer &rhs) const { return value == rhs.value && is_negative == rhs.is_negative; }

    // Unary operators
    constexpr integer operator-() const { return {value, !is_negative}; }

    constexpr integer operator+() const { return *this; }

    // Addition
    constexpr integer operator+(const integer &rhs) const {
        if (is_negative == rhs.is_negative) {
            // Same sign: add values and keep sign
            return {value + rhs.value, is_negative};
        } else {
            // Different signs: subtract smaller from larger and take sign of larger
            if (value > rhs.value) {
                return {value - rhs.value, is_negative};
            } else if (value < rhs.value) {
                return {rhs.value - value, rhs.is_negative};
            } else {
                return {0};
            }
        }
    }

    // Subtraction
    constexpr integer operator-(const integer &rhs) const { return *this + (-rhs); }

    // Multiplication
    constexpr integer operator*(const integer &rhs) const {
        auto result = value * rhs.value;
        return {result, (is_negative != rhs.is_negative) && (result != 0)};
    }

    // Division
    constexpr integer operator/(const integer &rhs) const {
        auto result = value / rhs.value;
        return {result, (is_negative != rhs.is_negative) && (result != 0)};
    }

    // Modulo
    constexpr integer operator%(const integer &rhs) const {
        auto result = value % rhs.value;
        return {result, is_negative && (result != 0)};
    }

    // Compound assignment operators
    constexpr integer &operator+=(const integer &rhs) {
        *this = *this + rhs;
        return *this;
    }

    constexpr integer &operator-=(const integer &rhs) {
        *this = *this - rhs;
        return *this;
    }

    constexpr integer &operator*=(const integer &rhs) {
        *this = *this * rhs;
        return *this;
    }

    constexpr integer &operator/=(const integer &rhs) {
        *this = *this / rhs;
        return *this;
    }

    constexpr integer &operator%=(const integer &rhs) {
        *this = *this % rhs;
        return *this;
    }

    // Increment/Decrement operators
    constexpr integer &operator++() {
        *this += integer(1);
        return *this;
    }

    constexpr integer operator++(int) {
        integer temp = *this;
        ++(*this);
        return temp;
    }

    constexpr integer &operator--() {
        *this -= integer(1);
        return *this;
    }

    constexpr integer operator--(int) {
        integer temp = *this;
        --(*this);
        return temp;
    }
};

constexpr auto operator+(positive a, integer b) {
    const auto result = a + (b.is_negative ? -b.value : b.value);
    if (b.is_negative && b.value > a) {
        return integer(std::numeric_limits<positive>::max() - result);
    } else {
        return integer(result);
    }
}

constexpr auto operator+(integer a, positive b) { return b + a; }

constexpr auto operator+(positive a, negative b) {
    auto result      = a - b.value;
    bool is_negative = b.value > a;
    result           = is_negative ? (std::numeric_limits<positive>::max() - result + 1) : result;
    return integer(result, is_negative);
}

constexpr auto operator+(integer a, negative b) {
    const auto result = a.value + (a.is_negative ? b.value : -b.value);
    if (b.value > a.value && !a.is_negative) {
        return integer(std::numeric_limits<positive>::max() - result + 1, true);
    } else {
        return integer(result, a.is_negative);
    }
}
constexpr auto operator+(negative a, integer b) { return b + a; }

constexpr auto operator-(integer a, negative b) {
    const auto result = a.value + (a.is_negative ? -b.value : b.value);
    if (a.is_negative && b.value > a.value) {
        return integer(std::numeric_limits<positive>::max() - result - 1, false);
    } else {
        return integer(result, a.value < b.value);
    }
}
constexpr auto operator-(negative a, integer b) {
    b.is_negative = !b.is_negative;

    const auto result = a.value + (b.is_negative ? b.value : -b.value);
    if (!b.is_negative && b.value > a.value) {
        return integer(std::numeric_limits<positive>::max() - result - 1, false);
    } else {
        return integer(result, true);
    }
}

constexpr auto operator+(negative a, positive b) { return b + a; }
constexpr auto operator+(negative a, negative b) { return negative(a.value + b.value); }
constexpr auto operator-(positive a, negative b) { return a + integer(b.value); }
constexpr auto operator-(negative a, positive b) { return negative(a.value + b); }
constexpr auto operator-(negative a, negative b) {
    auto result = a.value - b.value;
    if (b.value > a.value) {
        return integer(result - 1, true);
    } else {
        return integer(result);
    }
}

constexpr auto operator-(positive a, integer b) {
    const auto result = a - (b.is_negative ? -b.value : b.value);
    if (b.is_negative && b.value > a) {
        return integer(std::numeric_limits<positive>::max() - result);
    } else {
        return integer(result);
    }
}

constexpr auto operator-(integer a, positive b) { return b - a; }

constexpr auto operator""_neg(unsigned long long N) { return negative(N); }

// Implement basic

} // namespace cbor::tags