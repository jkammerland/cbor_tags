#pragma once

#include "cbor_tags/cbor.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace cbor {

class Encoder {
  public:
    template <typename T> static std::vector<std::byte> serialize(const T &value) {
        Encoder encoder;
        encoder.encodeValue(value);
        return encoder.data_;
    }

  protected:
    std::vector<std::byte> data_;

    void encodeValue(const Value &value) {
        std::visit([this](const auto &v) { this->encode(v); }, value);
    }

    void encodeUnsigned(std::uint64_t value, std::byte majorType) {
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

    void encode(std::uint64_t value) { encodeUnsigned(value, static_cast<std::byte>(0x00)); }

    void encode(std::int64_t value) {
        if (value >= 0) {
            encodeUnsigned(static_cast<std::uint64_t>(value), static_cast<std::byte>(0x00));
        } else {
            encodeUnsigned(static_cast<std::uint64_t>(-1 - value), static_cast<std::byte>(0x20));
        }
    }

    void encode(std::span<const std::byte> value) {
        encodeUnsigned(value.size(), static_cast<std::byte>(0x40));
        data_.insert(data_.end(), value.begin(), value.end());
    }

    void encode(std::string_view value) {
        encodeUnsigned(value.size(), static_cast<std::byte>(0x60));
        data_.insert(data_.end(), reinterpret_cast<const std::byte *>(value.data()),
                     reinterpret_cast<const std::byte *>(value.data() + value.size()));
    }

    // Handle std::string
    void encode(const std::string &value) { encode(std::string_view(value)); }

    // Handle const char*
    void encode(const char *value) { encode(std::string_view(value)); }

    void encode(const ArrayView &value) { data_.insert(data_.end(), value.data.begin(), value.data.end()); }

    void encode(const MapView &value) { data_.insert(data_.end(), value.data.begin(), value.data.end()); }

    void encode(const TagView &value) {
        encodeUnsigned(value.tag, static_cast<std::byte>(0xC0));
        data_.insert(data_.end(), value.data.begin(), value.data.end());
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
            encodeUnsigned(value.size(), static_cast<std::byte>(0x80));
            for (const auto &item : value) {
                encodeValue(item);
            }
        }
    }
    void                          encodeValue(const std::vector<Value> &value) { arrayEncoder(value); }
    template <std::size_t N> void encodeValue(const std::array<Value, N> &value) { arrayEncoder(value); }

    // Handle std::map and std::unordered_map
    template <typename T> void mapEncoder(const T &value) {
        if (value.empty()) {
            data_.push_back(static_cast<std::byte>(0xA0));
        } else {
            encodeUnsigned(value.size(), static_cast<std::byte>(0xA0));
            for (const auto &[key, item] : value) {
                encodeValue(key);
                encodeValue(item);
            }
        }
    }
    void encodeValue(const std::map<Value, Value> &value) { mapEncoder(value); }
    void encodeValue(const std::unordered_map<Value, Value> &value) { mapEncoder(value); }

  private:
    template <typename Container> void encodeArray(const Container &container) {
        encode(static_cast<std::uint64_t>(container.size()) | 0x80);
        for (const auto &item : container) {
            encodeValue(item);
        }
    }

    template <typename Map> void encodeMap(const Map &map) {
        encode(static_cast<std::uint64_t>(map.size()) | 0xA0);
        for (const auto &[key, value] : map) {
            encodeValue(key);
            encodeValue(value);
        }
    }
};

} // namespace cbor