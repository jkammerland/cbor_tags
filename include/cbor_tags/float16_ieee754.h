#pragma once

#include <bit>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>

namespace cbor::tags {

struct float16_t {
    std::uint16_t value;

    float16_t() = default;
    float16_t(std::uint16_t value) : value(value) {}
    float16_t(float f) { *this = f; }
    float16_t(double d) { *this = d; }

    // Keep encoded binary16 values usable as keys: signed zero and NaN payloads
    // remain distinct. Convert explicitly to float for numeric comparisons.
    friend constexpr bool operator==(float16_t lhs, float16_t rhs) noexcept { return lhs.value == rhs.value; }

    friend constexpr std::strong_ordering operator<=>(float16_t lhs, float16_t rhs) noexcept {
        return total_order_key(lhs.value) <=> total_order_key(rhs.value);
    }

    operator float() const {
        unsigned exp  = (value >> 10) & 0x1f;
        unsigned mant = value & 0x3ff;
        float    val;

        if (exp == 0) {
            val = std::ldexp(static_cast<float>(mant), -24);
        } else if (exp != 31) {
            val = std::ldexp(static_cast<float>(mant + 1024U), static_cast<int>(exp) - 25);
        } else {
            val = mant == 0 ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
        }

        return (value & 0x8000) ? -val : val;
    }

    operator double() const { return static_cast<double>(static_cast<float>(*this)); }

    float16_t &operator=(float f) {
        value = binary16_from_ieee_bits<8U, 23U, 127>(std::bit_cast<std::uint32_t>(f));
        return *this;
    }

    float16_t &operator=(double d) {
        value = binary16_from_ieee_bits<11U, 52U, 1023>(std::bit_cast<std::uint64_t>(d));
        return *this;
    }

  private:
    [[nodiscard]] static constexpr std::uint64_t round_right_to_even(std::uint64_t value, unsigned int shift) noexcept {
        if (shift == 0U) {
            return value;
        }

        const auto truncated      = value >> shift;
        const auto remainder_mask = (std::uint64_t{1} << shift) - 1U;
        const auto remainder      = value & remainder_mask;
        const auto halfway        = std::uint64_t{1} << (shift - 1U);

        return truncated + static_cast<std::uint64_t>(remainder > halfway || (remainder == halfway && (truncated & 1U) != 0U));
    }

    [[nodiscard]] static constexpr std::uint16_t total_order_key(std::uint16_t bits) noexcept {
        return (bits & 0x8000U) != 0U ? static_cast<std::uint16_t>(~bits) : static_cast<std::uint16_t>(bits | 0x8000U);
    }

    template <unsigned int ExponentBits, unsigned int FractionBits, int ExponentBias>
    [[nodiscard]] static constexpr std::uint16_t binary16_from_ieee_bits(std::uint64_t bits) noexcept {
        const auto sign          = static_cast<std::uint16_t>((bits >> (FractionBits + ExponentBits - 15U)) & 0x8000U);
        const auto exponent_mask = (std::uint64_t{1} << ExponentBits) - 1U;
        const auto fraction_mask = (std::uint64_t{1} << FractionBits) - 1U;
        const auto raw_exponent  = static_cast<unsigned int>((bits >> FractionBits) & exponent_mask);
        const auto fraction      = bits & fraction_mask;

        if (raw_exponent == exponent_mask) {
            return static_cast<std::uint16_t>(sign | (fraction == 0U ? 0x7C00U : 0x7E00U));
        }
        if (raw_exponent == 0U) {
            return sign;
        }

        const auto exponent = static_cast<int>(raw_exponent) - ExponentBias;
        if (exponent > 15) {
            return static_cast<std::uint16_t>(sign | 0x7C00U);
        }

        if (exponent >= -14) {
            auto half_exponent = exponent + 15;
            auto half_fraction = round_right_to_even(fraction, FractionBits - 10U);
            if (half_fraction == 0x400U) {
                half_fraction = 0U;
                ++half_exponent;
            }
            if (half_exponent >= 31) {
                return static_cast<std::uint16_t>(sign | 0x7C00U);
            }
            return static_cast<std::uint16_t>(sign | (static_cast<std::uint16_t>(half_exponent) << 10U) |
                                              static_cast<std::uint16_t>(half_fraction));
        }

        if (exponent < -25) {
            return sign;
        }

        const auto significand   = fraction | (std::uint64_t{1} << FractionBits);
        const auto shift         = static_cast<unsigned int>(static_cast<int>(FractionBits) - exponent - 24);
        const auto half_fraction = round_right_to_even(significand, shift);
        return static_cast<std::uint16_t>(sign | static_cast<std::uint16_t>(half_fraction));
    }
};
} // namespace cbor::tags

namespace std {
template <> struct hash<cbor::tags::float16_t> {
    size_t operator()(const cbor::tags::float16_t &f) const { return std::hash<std::uint16_t>{}(f.value); }
};
} // namespace std
