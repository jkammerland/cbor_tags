#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_detail.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace cbor::tags {

template <typename OutputBuffer = std::vector<std::byte>>
    requires ValidCborBuffer<OutputBuffer>
class encoder {
  public:
    using value_type = typename OutputBuffer::value_type;
    using size_type  = typename OutputBuffer::size_type;

    template <typename T> static auto serialize(const T &value) {
        OutputBuffer          data;
        encoder<OutputBuffer> encoder(data);
        encoder.encode_value(value);
        return data;
    }

    constexpr explicit encoder(OutputBuffer &data) : data_(data) {}

    constexpr void encode_value(const value &value) {
        std::visit([this](const auto &v) { this->encode(v); }, value);
    }

    constexpr void encode_unsigned(std::uint64_t value, value_type majorType) {
        if (value < 24) {
            appender_(data_, static_cast<value_type>(value) | majorType);
        } else if (value <= 0xFF) {
            appender_(data_, static_cast<value_type>(24) | majorType);
            appender_(data_, static_cast<value_type>(value));
        } else if (value <= 0xFFFF) {
            appender_(data_, static_cast<value_type>(25) | majorType);
            appender_(data_, static_cast<value_type>(value >> 8));
            appender_(data_, static_cast<value_type>(value));
        } else if (value <= 0xFFFFFFFF) {
            appender_(data_, static_cast<value_type>(26) | majorType);
            appender_(data_, static_cast<value_type>(value >> 24));
            appender_(data_, static_cast<value_type>(value >> 16));
            appender_(data_, static_cast<value_type>(value >> 8));
            appender_(data_, static_cast<value_type>(value));
        } else {
            appender_(data_, static_cast<value_type>(27) | majorType);
            appender_(data_, static_cast<value_type>(value >> 56));
            appender_(data_, static_cast<value_type>(value >> 48));
            appender_(data_, static_cast<value_type>(value >> 40));
            appender_(data_, static_cast<value_type>(value >> 32));
            appender_(data_, static_cast<value_type>(value >> 24));
            appender_(data_, static_cast<value_type>(value >> 16));
            appender_(data_, static_cast<value_type>(value >> 8));
            appender_(data_, static_cast<value_type>(value));
        }
    }

    constexpr void encode(std::uint64_t value) { encode_unsigned(value, static_cast<value_type>(0x00)); }

    constexpr void encode(std::int64_t value) {
        if (value >= 0) {
            encode_unsigned(static_cast<std::uint64_t>(value), static_cast<value_type>(0x00));
        } else {
            encode_unsigned(static_cast<std::uint64_t>(-1 - value), static_cast<value_type>(0x20));
        }
    }

    constexpr void encode(std::span<const std::byte> value) {
        encode_unsigned(value.size(), static_cast<value_type>(0x40));
        appender_(data_, value);
    }

    constexpr void encode(std::string_view value) {
        encode_unsigned(value.size(), static_cast<value_type>(0x60));
        appender_(data_, value);
    }

    // Handle std::string
    constexpr void encode(const std::string &value) { encode(std::string_view(value)); }

    // Handle const char*
    constexpr void encode(const char *value) { encode(std::string_view(value)); }

    constexpr void encode(const binary_array_view &value) { appender_(data_, value.data); }

    constexpr void encode(const binary_map_view &value) { appender_(data_, value.data); }

    constexpr void encode(const binary_tag_view &value) {
        encode_unsigned(value.tag, static_cast<value_type>(0xC0));
        appender_(data_, value.data);
    }

    constexpr void encode(float16_t value) {
        appender_(data_, static_cast<value_type>(0xf9)); // CBOR Float16 tag
        appender_(data_, static_cast<value_type>(value.value >> 8));
        appender_(data_, static_cast<value_type>(value.value & 0xff));
    }

    constexpr void encode(float value) {
        appender_(data_, static_cast<value_type>(0xFA));
        auto bits = std::bit_cast<std::uint32_t>(value);
        appender_(data_, static_cast<value_type>(bits >> 24));
        appender_(data_, static_cast<value_type>(bits >> 16));
        appender_(data_, static_cast<value_type>(bits >> 8));
        appender_(data_, static_cast<value_type>(bits));
    }

    constexpr void encode(double value) {
        appender_(data_, static_cast<value_type>(0xFB));
        auto bits = std::bit_cast<std::uint64_t>(value);
        appender_(data_, static_cast<value_type>(bits >> 56));
        appender_(data_, static_cast<value_type>(bits >> 48));
        appender_(data_, static_cast<value_type>(bits >> 40));
        appender_(data_, static_cast<value_type>(bits >> 32));
        appender_(data_, static_cast<value_type>(bits >> 24));
        appender_(data_, static_cast<value_type>(bits >> 16));
        appender_(data_, static_cast<value_type>(bits >> 8));
        appender_(data_, static_cast<value_type>(bits));
    }

    constexpr void encode(bool value) { appender_(data_, value ? static_cast<value_type>(0xF5) : static_cast<value_type>(0xF4)); }

    constexpr void encode(std::nullptr_t) { appender_(data_, static_cast<value_type>(0xF6)); }

    // Handle std::vector and std::array
    template <typename T> constexpr void arrayEncoder(const T &value) {
        if (value.empty()) {
            appender_(data_, static_cast<value_type>(0x80));
        } else {
            encode_unsigned(value.size(), static_cast<value_type>(0x80));
            for (const auto &item : value) {
                encode_value(item);
            }
        }
    }
    constexpr void                          encode_value(const std::vector<value> &value) { arrayEncoder(value); }
    template <std::size_t N> constexpr void encode_value(const std::array<value, N> &value) { arrayEncoder(value); }

    // Handle std::map and std::unordered_map
    template <typename T> constexpr void map_encoder(const T &value) {
        if (value.empty()) {
            appender_(data_, static_cast<value_type>(0xA0));
        } else {
            encode_unsigned(value.size(), static_cast<value_type>(0xA0));
            for (const auto &[key, item] : value) {
                encode_value(key);
                encode_value(item);
            }
        }
    }
    constexpr void encode_value(const std::map<value, value> &value) { map_encoder(value); }
    constexpr void encode_value(const std::unordered_map<value, value> &value) { map_encoder(value); }

  protected:
    detail::appender<OutputBuffer> appender_;
    OutputBuffer                  &data_;

    template <typename Container> constexpr void encode_array(const Container &container) {
        encode(static_cast<std::uint64_t>(container.size()) | 0x80);
        for (const auto &item : container) {
            encode_value(item);
        }
    }

    template <typename Map> constexpr void encode_map(const Map &map) {
        encode(static_cast<std::uint64_t>(map.size()) | 0xA0);
        for (const auto &[key, value] : map) {
            encode_value(key);
            encode_value(value);
        }
    }
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