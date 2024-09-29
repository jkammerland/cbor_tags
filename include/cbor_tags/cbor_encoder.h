#pragma once

#include "cbor_tags/cbor.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace cbor::tags {

class encoder {
  public:
    template <typename T> static std::vector<std::byte> serialize(const T &value) {
        encoder encoder;
        encoder.encode_value(value);
        return encoder.data_;
    }

    void encode_value(const value &value) {
        std::visit([this](const auto &v) { this->encode(v); }, value);
    }

    void encode_unsigned(std::uint64_t value, std::byte majorType) {
        if (value < 24) {
            data_.push_back(static_cast<std::byte>(value) | majorType);
        } else if (value <= 0xFF) {
            data_.push_back(static_cast<std::byte>(24) | majorType);
            data_.push_back(static_cast<std::byte>(value));
        } else if (value <= 0xFFFF) {
            data_.push_back(static_cast<std::byte>(25) | majorType);
            data_.push_back(static_cast<std::byte>(value >> 8));
            data_.push_back(static_cast<std::byte>(value));
        } else if (value <= 0xFFFFFFFF) {
            data_.push_back(static_cast<std::byte>(26) | majorType);
            data_.push_back(static_cast<std::byte>(value >> 24));
            data_.push_back(static_cast<std::byte>(value >> 16));
            data_.push_back(static_cast<std::byte>(value >> 8));
            data_.push_back(static_cast<std::byte>(value));
        } else {
            data_.push_back(static_cast<std::byte>(27) | majorType);
            data_.push_back(static_cast<std::byte>(value >> 56));
            data_.push_back(static_cast<std::byte>(value >> 48));
            data_.push_back(static_cast<std::byte>(value >> 40));
            data_.push_back(static_cast<std::byte>(value >> 32));
            data_.push_back(static_cast<std::byte>(value >> 24));
            data_.push_back(static_cast<std::byte>(value >> 16));
            data_.push_back(static_cast<std::byte>(value >> 8));
            data_.push_back(static_cast<std::byte>(value));
        }
    }

    void encode(std::uint64_t value) { encode_unsigned(value, static_cast<std::byte>(0x00)); }

    void encode(std::int64_t value) {
        if (value >= 0) {
            encode_unsigned(static_cast<std::uint64_t>(value), static_cast<std::byte>(0x00));
        } else {
            encode_unsigned(static_cast<std::uint64_t>(-1 - value), static_cast<std::byte>(0x20));
        }
    }

    void encode(std::span<const std::byte> value) {
        encode_unsigned(value.size(), static_cast<std::byte>(0x40));
        data_.insert(data_.end(), value.begin(), value.end());
    }

    void encode(std::string_view value) {
        encode_unsigned(value.size(), static_cast<std::byte>(0x60));
        data_.insert(data_.end(), reinterpret_cast<const std::byte *>(value.data()),
                     reinterpret_cast<const std::byte *>(value.data() + value.size()));
    }

    // Handle std::string
    void encode(const std::string &value) { encode(std::string_view(value)); }

    // Handle const char*
    void encode(const char *value) { encode(std::string_view(value)); }

    void encode(const array_view &value) { data_.insert(data_.end(), value.data.begin(), value.data.end()); }

    void encode(const map_view &value) { data_.insert(data_.end(), value.data.begin(), value.data.end()); }

    void encode(const tag_view &value) {
        encode_unsigned(value.tag, static_cast<std::byte>(0xC0));
        data_.insert(data_.end(), value.data.begin(), value.data.end());
    }

    void encode(float16_t value) {
        data_.push_back(static_cast<std::byte>(0xf9)); // CBOR Float16 tag
        data_.push_back(static_cast<std::byte>(value.value >> 8));
        data_.push_back(static_cast<std::byte>(value.value & 0xff));
    }

    void encode(float value) {
        data_.push_back(static_cast<std::byte>(0xFA));
        auto bits = std::bit_cast<std::uint32_t>(value);
        data_.push_back(static_cast<std::byte>(bits >> 24));
        data_.push_back(static_cast<std::byte>(bits >> 16));
        data_.push_back(static_cast<std::byte>(bits >> 8));
        data_.push_back(static_cast<std::byte>(bits));
    }

    void encode(double value) {
        data_.push_back(static_cast<std::byte>(0xFB));
        auto bits = std::bit_cast<std::uint64_t>(value);
        data_.push_back(static_cast<std::byte>(bits >> 56));
        data_.push_back(static_cast<std::byte>(bits >> 48));
        data_.push_back(static_cast<std::byte>(bits >> 40));
        data_.push_back(static_cast<std::byte>(bits >> 32));
        data_.push_back(static_cast<std::byte>(bits >> 24));
        data_.push_back(static_cast<std::byte>(bits >> 16));
        data_.push_back(static_cast<std::byte>(bits >> 8));
        data_.push_back(static_cast<std::byte>(bits));
    }

    void encode(bool value) { data_.push_back(value ? static_cast<std::byte>(0xF5) : static_cast<std::byte>(0xF4)); }

    void encode(std::nullptr_t) { data_.push_back(static_cast<std::byte>(0xF6)); }

    // Handle std::vector and std::array
    template <typename T> void arrayEncoder(const T &value) {
        if (value.empty()) {
            data_.push_back(static_cast<std::byte>(0x80));
        } else {
            encode_unsigned(value.size(), static_cast<std::byte>(0x80));
            for (const auto &item : value) {
                encode_value(item);
            }
        }
    }
    void                          encode_value(const std::vector<value> &value) { arrayEncoder(value); }
    template <std::size_t N> void encode_value(const std::array<value, N> &value) { arrayEncoder(value); }

    // Handle std::map and std::unordered_map
    template <typename T> void map_encoder(const T &value) {
        if (value.empty()) {
            data_.push_back(static_cast<std::byte>(0xA0));
        } else {
            encode_unsigned(value.size(), static_cast<std::byte>(0xA0));
            for (const auto &[key, item] : value) {
                encode_value(key);
                encode_value(item);
            }
        }
    }
    void encode_value(const std::map<value, value> &value) { map_encoder(value); }
    void encode_value(const std::unordered_map<value, value> &value) { map_encoder(value); }

  protected:
    std::vector<std::byte> data_;

    template <typename Container> void encode_array(const Container &container) {
        encode(static_cast<std::uint64_t>(container.size()) | 0x80);
        for (const auto &item : container) {
            encode_value(item);
        }
    }

    template <typename Map> void encode_map(const Map &map) {
        encode(static_cast<std::uint64_t>(map.size()) | 0xA0);
        for (const auto &[key, value] : map) {
            encode_value(key);
            encode_value(value);
        }
    }
};

} // namespace cbor::tags