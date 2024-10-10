#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_detail.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace cbor::tags {

template <typename T> struct iterator_type {
    using type = typename T::const_iterator;
};

template <typename T, std::size_t Extent> struct iterator_type<std::span<T, Extent>> {
    using type = typename std::span<T, Extent>::iterator;
};

template <typename InputBuffer = std::span<const std::byte>>
    requires ValidCborBuffer<InputBuffer>
class decoder {
  public:
    using size_type    = typename InputBuffer::size_type;
    using value_type   = typename InputBuffer::value_type;
    using iterator_t   = typename iterator_type<InputBuffer>::type;
    using cbor_variant = std::conditional_t<IsContiguous<InputBuffer>, value, value_ranged<std::ranges::subrange<iterator_t>>>;

    explicit decoder(const InputBuffer &data) : data_(data), reader_(data) {}

    static cbor_variant deserialize(const InputBuffer &data) {
        decoder decoder(data);
        return decoder.decode_value();
    }

    static std::vector<cbor_variant> deserialize(binary_array_view array) {
        decoder                   decoder(array.data);
        std::vector<cbor_variant> result;
        while (!decoder.reader_.empty(decoder.data_)) {
            result.push_back(decoder.decode_value());
        }
        return result;
    }

    template <typename Container> static void deserialize_to(binary_array_view array, Container &result) {
        decoder                     decoder(array.data);
        detail::appender<Container> appender_;

        while (!decoder.reader_.empty(decoder.data_)) {
            appender_(result, decoder.decode_value());
        }
    }

    template <typename MapType> static MapType deserialize(binary_map_view map) {
        decoder decoder(map.data);
        MapType result;
        while (!decoder.reader_.empty(decoder.data_)) {
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

    cbor_variant decode_value() {
        if (reader_.empty(data_)) {
            throw std::runtime_error("Unexpected end of input");
        }

        const value_type initialByte    = reader_.read(data_);
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

    auto decode_bstring(value_type additionalInfo) {
        auto length = decode_unsigned(additionalInfo); // TODO: fix me
        if (reader_.empty(data_, length - 1)) {
            throw std::runtime_error("Unexpected end of input");
        }

        if constexpr (IsContiguous<InputBuffer>) {
            auto result = std::span<const std::byte>(reinterpret_cast<const std::byte *>(&data_[reader_.position_]), length);
            reader_.position_ += length;
            return result;
        } else {
            return binary_array_range_view{std::ranges::subrange<iterator_t>(reader_.position_, std::next(reader_.position_, length))};
        }
    }

    auto decode_text(value_type additionalInfo) {
        auto bytes = decode_bstring(additionalInfo);
        if constexpr (IsContiguous<InputBuffer>) {
            return std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        } else {
            return char_range_view{bytes.range};
        }
    }

    auto decode_array(value_type additionalInfo) {
        auto length   = decode_unsigned(additionalInfo);
        auto startPos = reader_.position_;
        for (size_t i = 0; i < length; ++i) {
            decode_value();
        }

        if constexpr (IsContiguous<InputBuffer>) {
            return binary_array_view{
                std::span<const std::byte>(reinterpret_cast<const std::byte *>(&data_[startPos]), reader_.position_ - startPos)};
        } else {
            return binary_array_range_view{std::ranges::subrange(startPos, reader_.position_)};
        }
    }

    auto decode_map(value_type additionalInfo) {
        size_t length   = decode_unsigned(additionalInfo);
        auto   startPos = reader_.position_;
        for (size_t i = 0; i < length; ++i) {
            decode_value(); // key
            decode_value(); // value
        }

        if constexpr (IsContiguous<InputBuffer>) {
            return binary_map_view{
                std::span<const std::byte>(reinterpret_cast<const std::byte *>(&data_[startPos]), reader_.position_ - startPos)};
        } else {
            return binary_map_range_view{std::ranges::subrange(startPos, reader_.position_)};
        }
    }

    auto decode_tag(value_type additionalInfo) {
        auto tag  = decode_unsigned(additionalInfo);
        auto data = decode_value();
        if constexpr (IsContiguous<InputBuffer>) {
            return binary_tag_view{tag, std::get<std::span<const std::byte>>(data)};
        } else {
            return binary_tag_range_view{tag, std::get<binary_range_view<std::ranges::subrange<iterator_t>>>(data).range};
        }
    }

    cbor_variant decodeSimpleOrFloat(value_type additionalInfo) {
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
        if (reader_.empty(data_)) {
            throw std::runtime_error("Unexpected end of input");
        }
        return static_cast<uint8_t>(reader_.read(data_));
    }

    uint16_t read_uint16() {
        if (reader_.empty(data_, 1)) {
            throw std::runtime_error("Unexpected end of input");
        }
        uint16_t result = (static_cast<uint16_t>(reader_.read(data_)) << 8) | static_cast<uint16_t>(reader_.read(data_));
        return result;
    }

    uint32_t read_uint32() {
        if (reader_.empty(data_, 3)) {
            throw std::runtime_error("Unexpected end of input");
        }
        uint32_t result = (static_cast<uint32_t>(reader_.read(data_)) << 24) | (static_cast<uint32_t>(reader_.read(data_)) << 16) |
                          (static_cast<uint32_t>(reader_.read(data_)) << 8) | static_cast<uint32_t>(reader_.read(data_));
        return result;
    }

    uint64_t read_uint64() {
        if (reader_.empty(data_, 7)) {
            throw std::runtime_error("Unexpected end of input");
        }
        uint64_t result = (static_cast<uint64_t>(reader_.read(data_)) << 56) | (static_cast<uint64_t>(reader_.read(data_)) << 48) |
                          (static_cast<uint64_t>(reader_.read(data_)) << 40) | (static_cast<uint64_t>(reader_.read(data_)) << 32) |
                          (static_cast<uint64_t>(reader_.read(data_)) << 24) | (static_cast<uint64_t>(reader_.read(data_)) << 16) |
                          (static_cast<uint64_t>(reader_.read(data_)) << 8) | static_cast<uint64_t>(reader_.read(data_));
        return result;
    }

    // CBOR Float16 decoding function
    float16_t read_float16() {
        if (reader_.empty(data_, 1)) {
            throw std::runtime_error("Unexpected end of input");
        }

        std::uint16_t value = (static_cast<std::uint16_t>(reader_.read(data_)) << 8) | static_cast<std::uint16_t>(reader_.read(data_));
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
    const InputBuffer          &data_;
    detail::reader<InputBuffer> reader_;
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