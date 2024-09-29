#pragma once

#include "cbor_tags/cbor.h"

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace cbor::tags {

class decoder {
  public:
    static value deserialize(std::span<const std::byte> data) {
        decoder decoder(data);
        return decoder.decode_value();
    }

    static std::vector<value> deserialize(array_view array) {
        decoder            decoder(array.data);
        std::vector<value> result;
        while (decoder.position_ < array.data.size()) {
            result.push_back(decoder.decode_value());
        }
        return result;
    }

    template <typename MapType> static MapType deserialize(map_view map) {
        decoder decoder(map.data);
        MapType result;
        while (decoder.position_ < map.data.size()) {
            auto key    = decoder.decode_value();
            auto value  = decoder.decode_value();
            result[key] = value;
        }
        return result;
    }

  protected:
    std::span<const std::byte> data_;
    size_t                     position_ = 0;

    explicit decoder(std::span<const std::byte> data) : data_(data) {}

    value decode_value() {
        if (position_ >= data_.size()) {
            throw std::runtime_error("Unexpected end of input");
        }

        const std::byte initialByte    = data_[position_++];
        const auto      majorType      = static_cast<major_type>(static_cast<uint8_t>(initialByte) >> 5);
        const uint8_t   additionalInfo = static_cast<uint8_t>(initialByte) & 0x1F;

        switch (majorType) {
        case major_type::UnsignedInteger: return decode_unsigned(additionalInfo);
        case major_type::NegativeInteger: return decode_integer(additionalInfo);
        case major_type::ByteString: return decode_bstring(additionalInfo);
        case major_type::TextString: return decode_text(additionalInfo);
        case major_type::Array: return decode_array(additionalInfo);
        case major_type::Map: return decode_map(additionalInfo);
        case major_type::Tag: return decode_tag(additionalInfo);
        case major_type::SimpleOrFloat: return decodeSimpleOrFloat(additionalInfo);
        default: throw std::runtime_error("Unknown major type");
        }
    }

    uint64_t decode_unsigned(uint8_t additionalInfo) {
        if (additionalInfo < 24) {
            return additionalInfo;
        }
        return read_unsigned(additionalInfo);
    }

    int64_t decode_integer(uint8_t additionalInfo) {
        uint64_t value = decode_unsigned(additionalInfo);
        return -1 - static_cast<int64_t>(value);
    }

    std::span<const std::byte> decode_bstring(uint8_t additionalInfo) {
        size_t length = decode_unsigned(additionalInfo);
        if (position_ + length > data_.size()) {
            throw std::runtime_error("Unexpected end of input");
        }
        std::span<const std::byte> result = data_.subspan(position_, length);
        position_ += length;
        return result;
    }

    std::string_view decode_text(uint8_t additionalInfo) {
        auto bytes = decode_bstring(additionalInfo);
        return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
    }

    array_view decode_array(uint8_t additionalInfo) {
        size_t             length   = decode_unsigned(additionalInfo);
        size_t             startPos = position_;
        std::vector<value> items;
        items.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            items.push_back(decode_value());
        }
        return array_view{data_.subspan(startPos, position_ - startPos)};
    }

    map_view decode_map(uint8_t additionalInfo) {
        size_t length   = decode_unsigned(additionalInfo);
        size_t startPos = position_;
        for (size_t i = 0; i < length; ++i) {
            decode_value(); // key
            decode_value(); // value
        }
        return map_view{data_.subspan(startPos, position_ - startPos)};
    }

    tag_view decode_tag(uint8_t additionalInfo) {
        uint64_t tagValue = decode_unsigned(additionalInfo);
        size_t   startPos = position_;
        decode_value(); // tagged value
        return tag_view{tagValue, data_.subspan(startPos, position_ - startPos)};
    }

    value decodeSimpleOrFloat(uint8_t additionalInfo) {
        switch (additionalInfo) {
        case 20: return false;
        case 21: return true;
        case 22: return nullptr;
        case 26: return read_float();
        case 27: return read_double();
        default: throw std::runtime_error("Unsupported simple value or float");
        }
    }

    uint64_t read_unsigned(uint8_t additionalInfo) {
        switch (additionalInfo) {
        case 24: return read_uint8();
        case 25: return read_uint16();
        case 26: return read_uint32();
        case 27: return read_uint64();
        default: throw std::runtime_error("Invalid additional info for integer");
        }
    }

    uint8_t read_uint8() {
        if (position_ + 1 > data_.size()) {
            throw std::runtime_error("Unexpected end of input");
        }
        return static_cast<uint8_t>(data_[position_++]);
    }

    uint16_t read_uint16() {
        if (position_ + 2 > data_.size()) {
            throw std::runtime_error("Unexpected end of input");
        }
        uint16_t result = (static_cast<uint16_t>(data_[position_]) << 8) | static_cast<uint16_t>(data_[position_ + 1]);
        position_ += 2;
        return result;
    }

    uint32_t read_uint32() {
        if (position_ + 4 > data_.size()) {
            throw std::runtime_error("Unexpected end of input");
        }
        uint32_t result = (static_cast<uint32_t>(data_[position_]) << 24) | (static_cast<uint32_t>(data_[position_ + 1]) << 16) |
                          (static_cast<uint32_t>(data_[position_ + 2]) << 8) | static_cast<uint32_t>(data_[position_ + 3]);
        position_ += 4;
        return result;
    }

    uint64_t read_uint64() {
        if (position_ + 8 > data_.size()) {
            throw std::runtime_error("Unexpected end of input");
        }
        uint64_t result = (static_cast<uint64_t>(data_[position_]) << 56) | (static_cast<uint64_t>(data_[position_ + 1]) << 48) |
                          (static_cast<uint64_t>(data_[position_ + 2]) << 40) | (static_cast<uint64_t>(data_[position_ + 3]) << 32) |
                          (static_cast<uint64_t>(data_[position_ + 4]) << 24) | (static_cast<uint64_t>(data_[position_ + 5]) << 16) |
                          (static_cast<uint64_t>(data_[position_ + 6]) << 8) | static_cast<uint64_t>(data_[position_ + 7]);
        position_ += 8;
        return result;
    }

    float read_float() {
        uint32_t bits = read_uint32();
        return std::bit_cast<float>(bits);
    }

    double read_double() {
        uint64_t bits = read_uint64();
        return std::bit_cast<double>(bits);
    }
};

} // namespace cbor::tags