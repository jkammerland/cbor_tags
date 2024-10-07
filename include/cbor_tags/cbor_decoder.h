#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_detail.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace cbor::tags {

template <typename InputBuffer = std::span<const std::byte>>
    requires ValidCborBuffer<InputBuffer>
class decoder {
  public:
    using size_type  = typename InputBuffer::size_type;
    using value_type = typename InputBuffer::value_type;

    explicit decoder(const InputBuffer &data) : data_(data) {}

    static value deserialize(const InputBuffer &data) {
        decoder decoder(data);
        return decoder.decode_value();
    }

    static std::vector<value> deserialize(binary_array_view array) {
        decoder            decoder(array.data);
        std::vector<value> result;
        while (decoder.position_ < array.data.size()) {
            result.push_back(decoder.decode_value());
        }
        return result;
    }

    template <typename Container> static void deserialize_to(binary_array_view array, Container &result) {
        decoder decoder(array.data);
        while (decoder.position_ < array.data.size()) {
            result.push_back(decoder.decode_value());
        }
    }

    template <typename MapType> static MapType deserialize(binary_map_view map) {
        decoder decoder(map.data);
        MapType result;
        while (decoder.position_ < map.data.size()) {
            auto key    = decoder.decode_value();
            auto value  = decoder.decode_value();
            result[key] = value;
        }
        return result;
    }

    template <typename MapType> static void deserialize_to(binary_map_view map, MapType &result) {
        decoder decoder(map.data);
        while (decoder.position_ < map.data.size()) {
            auto key    = decoder.decode_value();
            auto value  = decoder.decode_value();
            result[key] = value;
        }
    }

    value decode_value() {
        if (position_ >= data_.size()) {
            throw std::runtime_error("Unexpected end of input");
        }

        const value_type initialByte    = data_[position_++];
        const auto       majorType      = static_cast<major_type>(static_cast<value_type>(initialByte) >> 5);
        const auto       additionalInfo = static_cast<value_type>(initialByte) & static_cast<value_type>(0x1F);

        switch (majorType) {
        case major_type::UnsignedInteger: return decode_unsigned(additionalInfo);
        case major_type::NegativeInteger: return decode_integer(additionalInfo);
        case major_type::ByteString: return decode_bstring(additionalInfo);
        case major_type::TextString: return decode_text(additionalInfo);
        case major_type::Array: return decode_array(additionalInfo);
        case major_type::Map: return decode_map(additionalInfo);
        case major_type::Tag: return decode_tag(additionalInfo);
        case major_type::SimpleOrFloat: return decodeSimpleOrFloat(additionalInfo);
        default: throw std::runtime_error("Unsupported major type");
        }
    }

    uint64_t decode_unsigned(value_type additionalInfo) {
        if (additionalInfo < static_cast<value_type>(24)) {
            return static_cast<uint64_t>(additionalInfo);
        }
        return read_unsigned(additionalInfo);
    }

    int64_t decode_integer(value_type additionalInfo) {
        uint64_t value = decode_unsigned(additionalInfo);
        return -1 - static_cast<int64_t>(value);
    }

    std::span<const std::byte> decode_bstring(value_type additionalInfo) {
        size_t length = decode_unsigned(additionalInfo);
        if (position_ + length > data_.size()) {
            throw std::runtime_error("Unexpected end of input");
        }
        auto result = std::span<const std::byte>(reinterpret_cast<const std::byte *>(&data_[position_]), length);
        position_ += length;
        return result;
    }

    std::string_view decode_text(value_type additionalInfo) {
        auto bytes = decode_bstring(additionalInfo);
        return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
    }

    binary_array_view decode_array(value_type additionalInfo) {
        auto length   = decode_unsigned(additionalInfo);
        auto startPos = position_;
        for (size_t i = 0; i < length; ++i) {
            decode_value();
        }
        return binary_array_view{std::span<const std::byte>(reinterpret_cast<const std::byte *>(&data_[startPos]), position_ - startPos)};
    }

    binary_map_view decode_map(value_type additionalInfo) {
        size_t length   = decode_unsigned(additionalInfo);
        size_t startPos = position_;
        for (size_t i = 0; i < length; ++i) {
            decode_value(); // key
            decode_value(); // value
        }
        return binary_map_view{std::span<const std::byte>(reinterpret_cast<const std::byte *>(&data_[startPos]), position_ - startPos)};
    }

    binary_tag_view decode_tag(value_type additionalInfo) {
        uint64_t tagValue = decode_unsigned(additionalInfo);
        size_t   startPos = position_;
        decode_value(); // tagged value
        return binary_tag_view{tagValue,
                               std::span<const std::byte>(reinterpret_cast<const std::byte *>(&data_[startPos]), position_ - startPos)};
    }

    value decodeSimpleOrFloat(value_type additionalInfo) {
        switch (static_cast<uint8_t>(additionalInfo)) {
        case 20: return false;
        case 21: return true;
        case 22: return nullptr;
        case 25: return read_float16();
        case 26: return read_float();
        case 27: return read_double();
        default: throw std::runtime_error("Unsupported simple value or float");
        }
    }

    uint64_t read_unsigned(value_type additionalInfo) {
        switch (static_cast<uint8_t>(additionalInfo)) {
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

    // CBOR Float16 decoding function
    float16_t read_float16() {
        if (position_ + 2 > data_.size()) {
            throw std::runtime_error("Unexpected end of input");
        }

        std::uint16_t value = (static_cast<std::uint16_t>(data_[position_]) << 8) | static_cast<std::uint16_t>(data_[position_ + 1]);
        position_ += 2;
        return float16_t{value};
    }

    float read_float() {
        uint32_t bits = read_uint32();
        return std::bit_cast<float>(bits);
    }

    double read_double() {
        uint64_t bits = read_uint64();
        return std::bit_cast<double>(bits);
    }

  protected:
    const InputBuffer &data_;
    size_type          position_ = 0;
};

template <typename InputBuffer> inline auto make_decoder(InputBuffer &buffer) { return decoder<InputBuffer>(buffer); }

template <typename InputBuffer = std::vector<std::byte>> inline auto make_data_and_decoder() {
    struct data_and_decoder {
        data_and_decoder() : data(), dec(data) {}
        InputBuffer          data;
        decoder<InputBuffer> dec;
    };

    return data_and_decoder{};
}

} // namespace cbor::tags