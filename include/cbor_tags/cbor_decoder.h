#pragma once

#include "cbor_tags/cbor.h"

#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace cbor {

class Decoder {
  public:
    static Value deserialize(std::span<const std::byte> data) {
        Decoder decoder(data);
        return decoder.decodeValue();
    }

    static std::vector<Value> deserialize(ArrayView array) {
        Decoder            decoder(array.data);
        std::vector<Value> result;
        while (decoder.position_ < array.data.size()) {
            result.push_back(decoder.decodeValue());
        }
        return result;
    }

    template <typename MapType> static MapType deserialize(MapView map) {
        Decoder decoder(map.data);
        MapType result;
        while (decoder.position_ < map.data.size()) {
            auto key    = decoder.decodeValue();
            auto value  = decoder.decodeValue();
            result[key] = value;
        }
        return result;
    }

  protected:
    std::span<const std::byte> data_;
    size_t                     position_ = 0;

    explicit Decoder(std::span<const std::byte> data) : data_(data) {}

    Value decodeValue() {
        if (position_ >= data_.size()) {
            throw std::runtime_error("Unexpected end of input");
        }

        std::byte initialByte    = data_[position_++];
        Type      majorType      = static_cast<Type>(static_cast<uint8_t>(initialByte) >> 5);
        uint8_t   additionalInfo = static_cast<uint8_t>(initialByte) & 0x1F;

        switch (majorType) {
        case Type::UnsignedInteger: return decodeUnsignedInteger(additionalInfo);
        case Type::NegativeInteger: return decodeNegativeInteger(additionalInfo);
        case Type::ByteString: return decodeByteString(additionalInfo);
        case Type::TextString: return decodeTextString(additionalInfo);
        case Type::Array: return decodeArray(additionalInfo);
        case Type::Map: return decodeMap(additionalInfo);
        case Type::Tag: return decodeTag(additionalInfo);
        case Type::SimpleOrFloat: return decodeSimpleOrFloat(additionalInfo);
        default: throw std::runtime_error("Unknown major type");
        }
    }

    uint64_t decodeUnsignedInteger(uint8_t additionalInfo) {
        if (additionalInfo < 24) {
            return additionalInfo;
        }
        return readUnsignedInteger(additionalInfo);
    }

    int64_t decodeNegativeInteger(uint8_t additionalInfo) {
        uint64_t value = decodeUnsignedInteger(additionalInfo);
        return -1 - static_cast<int64_t>(value);
    }

    std::span<const std::byte> decodeByteString(uint8_t additionalInfo) {
        size_t length = decodeUnsignedInteger(additionalInfo);
        if (position_ + length > data_.size()) {
            throw std::runtime_error("Unexpected end of input");
        }
        std::span<const std::byte> result = data_.subspan(position_, length);
        position_ += length;
        return result;
    }

    std::string_view decodeTextString(uint8_t additionalInfo) {
        auto bytes = decodeByteString(additionalInfo);
        return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
    }

    ArrayView decodeArray(uint8_t additionalInfo) {
        size_t             length   = decodeUnsignedInteger(additionalInfo);
        size_t             startPos = position_;
        std::vector<Value> items;
        items.reserve(length);
        for (size_t i = 0; i < length; ++i) {
            items.push_back(decodeValue());
        }
        return ArrayView{data_.subspan(startPos, position_ - startPos)};
    }

    MapView decodeMap(uint8_t additionalInfo) {
        size_t length   = decodeUnsignedInteger(additionalInfo);
        size_t startPos = position_;
        for (size_t i = 0; i < length; ++i) {
            decodeValue(); // key
            decodeValue(); // value
        }
        return MapView{data_.subspan(startPos, position_ - startPos)};
    }

    TagView decodeTag(uint8_t additionalInfo) {
        uint64_t tagValue = decodeUnsignedInteger(additionalInfo);
        size_t   startPos = position_;
        decodeValue(); // tagged value
        return TagView{tagValue, data_.subspan(startPos, position_ - startPos)};
    }

    Value decodeSimpleOrFloat(uint8_t additionalInfo) {
        switch (additionalInfo) {
        case 20: return false;
        case 21: return true;
        case 22: return nullptr;
        case 26: return readFloat();
        case 27: return readDouble();
        default: throw std::runtime_error("Unsupported simple value or float");
        }
    }

    uint64_t readUnsignedInteger(uint8_t additionalInfo) {
        switch (additionalInfo) {
        case 24: return readUint8();
        case 25: return readUint16();
        case 26: return readUint32();
        case 27: return readUint64();
        default: throw std::runtime_error("Invalid additional info for integer");
        }
    }

    uint8_t readUint8() {
        if (position_ + 1 > data_.size()) {
            throw std::runtime_error("Unexpected end of input");
        }
        return static_cast<uint8_t>(data_[position_++]);
    }

    uint16_t readUint16() {
        if (position_ + 2 > data_.size()) {
            throw std::runtime_error("Unexpected end of input");
        }
        uint16_t result = (static_cast<uint16_t>(data_[position_]) << 8) | static_cast<uint16_t>(data_[position_ + 1]);
        position_ += 2;
        return result;
    }

    uint32_t readUint32() {
        if (position_ + 4 > data_.size()) {
            throw std::runtime_error("Unexpected end of input");
        }
        uint32_t result = (static_cast<uint32_t>(data_[position_]) << 24) | (static_cast<uint32_t>(data_[position_ + 1]) << 16) |
                          (static_cast<uint32_t>(data_[position_ + 2]) << 8) | static_cast<uint32_t>(data_[position_ + 3]);
        position_ += 4;
        return result;
    }

    uint64_t readUint64() {
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

    float readFloat() {
        uint32_t bits = readUint32();
        return std::bit_cast<float>(bits);
    }

    double readDouble() {
        uint64_t bits = readUint64();
        return std::bit_cast<double>(bits);
    }
};

} // namespace cbor