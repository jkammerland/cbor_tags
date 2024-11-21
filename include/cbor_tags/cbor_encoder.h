#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_integer.h"
#include "cbor_tags/cbor_operators.h"
#include "cbor_tags/cbor_reflection.h"
#include "cbor_tags/cbor_simple.h"

#include <bit>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
// #include <fmt/base.h>
// #include <nameof.hpp>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace cbor::tags {

template <typename T> struct cbor_header_encoder;

template <typename OutputBuffer = std::vector<std::byte>, template <typename> typename... Encoders>
    requires ValidCborBuffer<OutputBuffer>
struct encoder : public Encoders<encoder<OutputBuffer, Encoders...>>... {
    using self_t = encoder<OutputBuffer, Encoders...>;
    using Encoders<self_t>::encode...;

    using byte_type  = typename OutputBuffer::value_type;
    using size_type  = typename OutputBuffer::size_type;
    using iterator_t = typename detail::iterator_type<OutputBuffer>::type;
    using subrange   = std::ranges::subrange<iterator_t>;
    using variant    = variant_t<OutputBuffer>;

    constexpr explicit encoder(OutputBuffer &data) : data_(data) {}

    template <typename... T> constexpr void operator()(const T &...args) { (encode(args), ...); }

    constexpr void encode_major_and_size(std::uint64_t value, byte_type majorType) {
        if (value < 24) {
            appender_(data_, static_cast<byte_type>(value) | majorType);
        } else if (value <= 0xFF) {
            appender_(data_, static_cast<byte_type>(24) | majorType);
            appender_(data_, static_cast<byte_type>(value));
        } else if (value <= 0xFFFF) {
            appender_(data_, static_cast<byte_type>(25) | majorType);
            appender_(data_, static_cast<byte_type>(value >> 8));
            appender_(data_, static_cast<byte_type>(value));
        } else if (value <= 0xFFFFFFFF) {
            appender_(data_, static_cast<byte_type>(26) | majorType);
            appender_(data_, static_cast<byte_type>(value >> 24));
            appender_(data_, static_cast<byte_type>(value >> 16));
            appender_(data_, static_cast<byte_type>(value >> 8));
            appender_(data_, static_cast<byte_type>(value));
        } else {
            appender_(data_, static_cast<byte_type>(27) | majorType);
            appender_(data_, static_cast<byte_type>(value >> 56));
            appender_(data_, static_cast<byte_type>(value >> 48));
            appender_(data_, static_cast<byte_type>(value >> 40));
            appender_(data_, static_cast<byte_type>(value >> 32));
            appender_(data_, static_cast<byte_type>(value >> 24));
            appender_(data_, static_cast<byte_type>(value >> 16));
            appender_(data_, static_cast<byte_type>(value >> 8));
            appender_(data_, static_cast<byte_type>(value));
        }
    }

    template <IsUnsigned T> constexpr void encode(T value) { encode_major_and_size(value, static_cast<byte_type>(0x00)); }

    template <IsSigned T> constexpr void encode(T value) {
        if (value >= 0) {
            encode_major_and_size(static_cast<std::uint64_t>(value), static_cast<byte_type>(0x00));
        } else {
            encode_major_and_size(static_cast<std::uint64_t>(-1 - value), static_cast<byte_type>(0x20));
        }
    }

    constexpr void encode(negative value) {
        encode_major_and_size(static_cast<std::uint64_t>(-1 - value.value), static_cast<byte_type>(0x20));
    }

    constexpr void encode(integer value) {
        if (value.is_negative) {
            encode(negative{value.value});
        } else {
            encode(value.value);
        }
    }

    template <std::uint64_t N> constexpr void encode(static_tag<N>) { encode_major_and_size(N, static_cast<byte_type>(0xC0)); }
    template <IsUnsigned T> constexpr void    encode(dynamic_tag<T> value) {
        encode_major_and_size(value.value, static_cast<byte_type>(0xC0));
    }

    template <IsString T> constexpr void encode(const T &value) {
        encode_major_and_size(value.size(), static_cast<byte_type>(get_major_3_bit_tag<T>()));
        appender_(data_, value);
    }

    template <IsRangeOfCborValues T> constexpr void encode(const T &value) {
        encode_major_and_size(value.size(), static_cast<byte_type>(IsMap<T> ? 0xA0 : 0x80));
        for (const auto &item : value) {
            encode(item);
        }
    }

    template <IsTaggedTuple T> constexpr void encode(const T &value) {
        encode_major_and_size(value.first, static_cast<byte_type>(0xC0));
        encode(value.second);
    }

    template <IsAggregate T> constexpr void encode(const T &value) {
        if constexpr (HasInlineTag<T>) {
            encode_major_and_size(T::cbor_tag, static_cast<byte_type>(0xC0));
        }
        const auto &tuple = to_tuple(value);
        std::apply([this](const auto &...args) { (this->encode(args), ...); }, tuple);
    }

    template <IsUntaggedTuple T> constexpr void encode(const T &value) {
        std::apply([this](const auto &...args) { (this->encode(args), ...); }, value);
    }

    constexpr void encode(float16_t value) {
        appender_(data_, static_cast<byte_type>(0xf9)); // CBOR Float16 tag
        appender_(data_, static_cast<byte_type>(value.value >> 8));
        appender_(data_, static_cast<byte_type>(value.value & 0xff));
    }

    constexpr void encode(float value) {
        appender_(data_, static_cast<byte_type>(0xFA));
        auto bits = std::bit_cast<std::uint32_t>(value);
        appender_(data_, static_cast<byte_type>(bits >> 24));
        appender_(data_, static_cast<byte_type>(bits >> 16));
        appender_(data_, static_cast<byte_type>(bits >> 8));
        appender_(data_, static_cast<byte_type>(bits));
    }

    constexpr void encode(double value) {
        appender_(data_, static_cast<byte_type>(0xFB));
        auto bits = std::bit_cast<std::uint64_t>(value);
        appender_(data_, static_cast<byte_type>(bits >> 56));
        appender_(data_, static_cast<byte_type>(bits >> 48));
        appender_(data_, static_cast<byte_type>(bits >> 40));
        appender_(data_, static_cast<byte_type>(bits >> 32));
        appender_(data_, static_cast<byte_type>(bits >> 24));
        appender_(data_, static_cast<byte_type>(bits >> 16));
        appender_(data_, static_cast<byte_type>(bits >> 8));
        appender_(data_, static_cast<byte_type>(bits));
    }

    constexpr void encode(bool value) { appender_(data_, value ? static_cast<byte_type>(0xF5) : static_cast<byte_type>(0xF4)); }

    constexpr void encode(std::nullptr_t) { appender_(data_, static_cast<byte_type>(0xF6)); }

    constexpr void encode(simple value) {
        if (value.value < 24 || value.value > 31) {
            encode_major_and_size(value.value, static_cast<byte_type>(0xE0));
        } else {
            throw std::runtime_error("Invalid simple value, use float16_t, float etc");
        }
    }

    template <typename T> constexpr void encode(const std::optional<T> &value) {
        if (value.has_value()) {
            encode(*value);
        } else {
            appender_(data_, static_cast<byte_type>(0xF6));
        }
    }

    template <typename... T> constexpr void encode(const std::variant<T...> &value) {
        std::visit([this](const auto &v) { this->encode(v); }, value);
    }

    // Variadic friends only in c++26, must be public
    detail::appender<OutputBuffer> appender_;
    OutputBuffer                  &data_;
};

template <typename T> struct enum_encoder {
    template <IsEnum U> constexpr void encode(U value) {
        detail::underlying<T>(this).encode(static_cast<std::underlying_type_t<U>>(value));
    }
};

template <typename T> struct cbor_header_encoder {
    constexpr void encode(as_array value) {
        detail::underlying<T>(this).encode_major_and_size(value.size_, static_cast<typename T::byte_type>(0x80));
    }
    constexpr void encode(as_map value) {
        detail::underlying<T>(this).encode_major_and_size(value.size_, static_cast<typename T::byte_type>(0xA0));
    }
};

template <typename OutputBuffer> inline auto make_encoder(OutputBuffer &buffer) {
    return encoder<OutputBuffer, cbor_header_encoder, enum_encoder>(buffer);
}
} // namespace cbor::tags