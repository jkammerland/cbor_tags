#pragma once

#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace cbor::tags {

struct float16_t {
    std::uint16_t value;

    float16_t() = default;
    float16_t(std::uint16_t value) : value(value) {}
    float16_t(float f) { *this = f; }

    operator float() const {
        unsigned exp  = (value >> 10) & 0x1f;
        unsigned mant = value & 0x3ff;
        float    val;

        if (exp == 0) {
            val = std::ldexp(mant, -24);
        } else if (exp != 31) {
            val = std::ldexp(mant + 1024, exp - 25);
        } else {
            val = mant == 0 ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
        }

        return (value & 0x8000) ? -val : val;
    }

    float16_t &operator=(float f) {
        std::uint32_t x;
        std::memcpy(&x, &f, sizeof(float));

        std::uint32_t sign     = (x >> 16) & 0x8000;
        int           exponent = ((x >> 23) & 0xff) - 127;
        std::uint32_t mantissa = x & 0x7fffff;

        if (exponent > 15) {
            // Overflow, set to infinity
            value = sign | 0x7c00;
        } else if (exponent < -14) {
            // Underflow
            if (exponent >= -24) {
                // Denormalized
                mantissa |= 0x800000;
                value = sign | (mantissa >> (-(exponent + 14) + 1));
            } else {
                // Zero
                value = sign;
            }
        } else {
            // Normalized
            value = sign | ((exponent + 15) << 10) | (mantissa >> 13);
        }

        return *this;
    }
};
} // namespace cbor::tags

namespace std {
template <> struct hash<cbor::tags::float16_t> {
    size_t operator()(const cbor::tags::float16_t &f) const { return std::hash<float>{}(f.value); }
};
} // namespace std