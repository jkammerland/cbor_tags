#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_operators.h"
#include "cbor_tags/cbor_reflection.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fmt/base.h>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace cbor::tags {

template <typename OutputBuffer = std::vector<std::byte>, typename... Encoders>
    requires ValidCborBuffer<OutputBuffer>
class encoder : public Encoders... {
  public:
    using Encoders::encode...;

    using byte_type  = typename OutputBuffer::value_type;
    using size_type  = typename OutputBuffer::size_type;
    using iterator_t = typename detail::iterator_type<OutputBuffer>::type;
    using subrange   = std::ranges::subrange<iterator_t>;
    using variant    = variant_t<OutputBuffer>;

    constexpr explicit encoder(OutputBuffer &data) : data_(data) {}

    template <typename T> static auto serialize(const T &value) {
        OutputBuffer          data;
        encoder<OutputBuffer> encoder(data);
        encoder.encode(value);
        return data;
    }

    template <typename T> constexpr void operator()(const T &value) { encode(value); }

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

    constexpr void encode(std::uint64_t value) { encode_major_and_size(value, static_cast<byte_type>(0x00)); }

    constexpr void encode(std::int64_t value) {
        if (value >= 0) {
            encode_major_and_size(static_cast<std::uint64_t>(value), static_cast<byte_type>(0x00));
        } else {
            encode_major_and_size(static_cast<std::uint64_t>(-1 - value), static_cast<byte_type>(0x20));
        }
    }

    template <typename T>
        requires std::is_unsigned_v<T> && std::is_integral_v<T>
    constexpr void encode(T value) {
        encode(static_cast<std::uint64_t>(value));
    }

    template <typename T>
        requires std::is_signed_v<T> && std::is_integral_v<T>
    constexpr void encode(T value) {
        encode(static_cast<std::int64_t>(value));
    }

    constexpr void encode(std::span<const std::byte> value) {
        encode_major_and_size(value.size(), static_cast<byte_type>(0x40));
        appender_(data_, value);
    }

    constexpr void encode(std::span<std::byte> value) {
        encode_major_and_size(value.size(), static_cast<byte_type>(0x40));
        appender_(data_, value);
    }

    constexpr void encode(std::string_view value) {
        encode_major_and_size(value.size(), static_cast<byte_type>(0x60));
        appender_(data_, value);
    }

    constexpr void encode(const std::string &value) { encode(std::string_view(value)); }

    constexpr void encode(const char *value) { encode(std::string_view(value)); }

    constexpr void encode(const binary_array_view &value) { appender_(data_, value.data); }

    constexpr void encode(const binary_map_view &value) { appender_(data_, value.data); }

    constexpr void encode(const binary_tag_view &value) {
        encode_major_and_size(value.tag, static_cast<byte_type>(0xC0));
        appender_(data_, value.data);
    }

    constexpr void encode(const char_range_view<subrange> &value) { appender_(data_, value.range); }
    constexpr void encode(const binary_range_view<subrange> &value) { appender_(data_, value.range); }
    constexpr void encode(const binary_array_range_view<subrange> &value) { appender_(data_, value.range); }
    constexpr void encode(const binary_map_range_view<subrange> &value) { appender_(data_, value.range); }
    constexpr void encode(const binary_tag_range_view<subrange> &value) { appender_(data_, value.range); }

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

    template <typename T>
        requires std::is_compound_v<T>
    constexpr void encode(const T &value) {
        // check if value is a range of some sort, i.e vector list etc
        if constexpr (IsRange<T>) {
            // If map, use 0xA0, otherwise array 0x80
            static_assert(!std::is_same_v<T, std::string>, "No strings allowed");
            constexpr auto major_type = IsMap<T> ? static_cast<byte_type>(0xA0) : static_cast<byte_type>(0x80);
            encode_major_and_size(value.size(), major_type);
            for (const auto &item : value) {
                encode(item);
            }
        }
        // Not range? Maybe T is a pair, e.g std::pair<cbor::tags::tag<i>, T::second_type>
        else if constexpr (IsTaggedTuple<T>) {
            static_assert(!HasCborTag<std::decay_t<decltype(T::second)>>, "The tagged type must not directly have a tag of its own");
            encode_major_and_size(value.first, static_cast<byte_type>(0xC0));
            encode(value.second);
        }
        // Is compound, not range, not tagged pair... check if struct
        else if constexpr (IsAggregate<T>) {
            // Check if T is has a tag
            if constexpr (HasCborTag<T>) {
                encode_major_and_size(T::cbor_tag, static_cast<byte_type>(0xC0));
            }
            const auto &tuple = to_tuple(value);
            // Apply encode to each element in tuple
            std::apply([this](const auto &...args) { (this->encode(args), ...); }, tuple);
        } else {
            std::apply([this](const auto &...args) { (this->encode(args), ...); }, value);
        }
    }

    template <typename T> constexpr void encode(const std::optional<T> &value) {
        if (value.has_value()) {
            encode(*value);
        } else {
            appender_(data_, static_cast<byte_type>(0xF6));
        }
    }

    template <typename... T>
        requires IsVariant<std::variant<T...>> && (!std::is_same_v<variant, std::variant<T...>>)
    constexpr void encode(const std::variant<T...> &value) {
        std::visit([this](const auto &v) { this->encode(v); }, value);
    }

    constexpr void encode(const variant &value) {
        std::visit([this](const auto &v) { this->encode(v); }, value);
    }

  protected:
    detail::appender<OutputBuffer> appender_;
    OutputBuffer                  &data_;
};

template <typename OutputBuffer> inline auto make_encoder(OutputBuffer &buffer) { return encoder<OutputBuffer>(buffer); }

template <typename OutputBuffer = std::vector<std::byte>> inline auto make_data_and_encoder() {
    struct data_and_encoder {
        data_and_encoder() : data(), enc(data) {}
        OutputBuffer          data;
        encoder<OutputBuffer> enc;
    };

    return data_and_encoder{};
}

} // namespace cbor::tags