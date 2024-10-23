#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_operators.h"
#include "cbor_tags/cbor_reflection.h"

#include <cstddef>
#include <cstdint>
#include <fmt/base.h>
#include <iostream>
#include <iterator>
#include <map>
#include <nameof.hpp>
#include <optional>
#include <ostream>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cbor::tags {

template <typename InputBuffer = std::span<const std::byte>>
    requires ValidCborBuffer<InputBuffer>
class decoder {
  public:
    using size_type  = typename InputBuffer::size_type;
    using byte_type  = typename InputBuffer::value_type;
    using iterator_t = typename detail::iterator_type<InputBuffer>::type;
    using subrange   = std::ranges::subrange<iterator_t>;
    using variant    = variant_t<InputBuffer>;

    explicit decoder(const InputBuffer &data) : data_(data), reader_(data) {}

    template <typename... T> constexpr void operator()(T &...args) { (decode(args), ...); }
    template <typename T> constexpr         operator T() { return decode<T>(); }

    template <typename T> constexpr T decode() {
        T result;
        decode(result);
        return result;
    }

    static variant deserialize(const InputBuffer &data) {
        decoder decoder(data);
        return decoder.decode_value();
    }

    static std::vector<variant> deserialize(binary_array_view array) {
        decoder              decoder(array.data);
        std::vector<variant> result;
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

    variant decode_value() {
        if (reader_.empty(data_)) {
            throw std::runtime_error("Unexpected end of input");
        }

        const auto initialByte    = reader_.read(data_);
        const auto majorType      = static_cast<major_type>(static_cast<std::byte>(initialByte) >> 5);
        const auto additionalInfo = initialByte & static_cast<byte_type>(0x1F);

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

    template <typename T> constexpr auto decode_value(T &value) {
        if (reader_.empty(data_)) {
            throw std::runtime_error("Unexpected end of input");
        }

        const auto initialByte    = reader_.read(data_);
        const auto additionalInfo = initialByte & static_cast<byte_type>(0x1F);

        decode(value, additionalInfo);
    }

    template <typename T>
        requires std::is_integral_v<T>
    constexpr void decode(T &value, byte_type additionalInfo) {
        const auto decoded = decode_unsigned(additionalInfo);

        if constexpr (std::is_signed_v<T>) {
            if (decoded > std::numeric_limits<T>::max() / 2) {
                value = -1 - static_cast<T>(decoded);
            } else {
                value = static_cast<T>(decoded);
            }
        } else {
            value = static_cast<T>(decoded);
        }
    }

    constexpr void decode(bool &value, byte_type additionalInfo) {
        if (additionalInfo == static_cast<byte_type>(20)) {
            value = false;
        } else if (additionalInfo == static_cast<byte_type>(21)) {
            value = true;
        } else {
            throw std::runtime_error("Invalid additional info for boolean");
        }
    }

    constexpr void decode(std::nullptr_t &value, byte_type additionalInfo) {
        if (additionalInfo != static_cast<byte_type>(22)) {
            throw std::runtime_error("Invalid additional info for null");
        }
        value = nullptr;
    }

    constexpr void decode(float16_t &value, byte_type additionalInfo) {
        if (additionalInfo != static_cast<byte_type>(25)) {
            throw std::runtime_error("Invalid additional info for float16");
        }
        value = read_float16();
    }

    constexpr void decode(float &value, byte_type additionalInfo) {
        if (additionalInfo != static_cast<byte_type>(26)) {
            throw std::runtime_error("Invalid additional info for float");
        }
        value = read_float();
    }

    constexpr void decode(double &value, byte_type additionalInfo) {
        if (additionalInfo != static_cast<byte_type>(27)) {
            throw std::runtime_error("Invalid additional info for double");
        }
        value = read_double();
    }

    constexpr void decode(std::string &value, byte_type additionalInfo) { value = std::string(decode_text(additionalInfo)); }

    constexpr void decode(std::string_view &value, byte_type additionalInfo) { value = decode_text(additionalInfo); }

    constexpr void decode(binary_array_view &value, byte_type additionalInfo) { value = decode_array(additionalInfo); }

    constexpr void decode(binary_map_view &value, byte_type additionalInfo) { value = decode_map(additionalInfo); }

    constexpr void decode(binary_tag_view &value, byte_type additionalInfo) { value = decode_tag(additionalInfo); }

    constexpr void decode(char_range_view<subrange> &value, byte_type additionalInfo) { value = decode_text(additionalInfo); }

    constexpr void decode(binary_range_view<subrange> &value, byte_type additionalInfo) { value = decode_bstring(additionalInfo); }

    constexpr void decode(binary_array_range_view<subrange> &value, byte_type additionalInfo) { value = decode_array(additionalInfo); }

    constexpr void decode(binary_map_range_view<subrange> &value, byte_type additionalInfo) { value = decode_map(additionalInfo); }

    constexpr void decode(binary_tag_range_view<subrange> &value, byte_type additionalInfo) { value = decode_tag(additionalInfo); }

    constexpr void decode(variant &value, byte_type additionalInfo) {
        if (additionalInfo == static_cast<byte_type>(20) || additionalInfo == static_cast<byte_type>(21)) {
            value = additionalInfo == static_cast<byte_type>(21);
        } else if (additionalInfo == static_cast<byte_type>(22)) {
            value = nullptr;
        } else if (additionalInfo >= static_cast<byte_type>(25) && additionalInfo <= static_cast<byte_type>(27)) {
            switch (additionalInfo) {
            case 25: value = read_float16(); break;
            case 26: value = read_float(); break;
            case 27: value = read_double(); break;
            default: throw std::runtime_error("Invalid additional info for float");
            }
        } else {
            value = decode_value();
        }
    }

    template <typename T> constexpr void decode(std::optional<T> &value, byte_type additionalInfo) {
        if (additionalInfo == static_cast<byte_type>(22)) {
            value.reset();
        } else {
            using value_type = std::decay_t<T>;
            value_type t;
            decode(t, additionalInfo);
            value = std::move(t);
        }
    }

    template <typename... T> constexpr void decode(std::variant<T...> &value) {
        const auto [major, additionalInfo] = read_initial_byte();
        if (!is_valid_major<decltype(major), T...>(major)) {
            throw std::runtime_error("Invalid major type for variant");
        }

        // Only compare against the types in the variant
        bool found_type_in_variant = ([this, major, additionalInfo, &value]<typename U>() -> bool {
            if (ConceptType<byte_type, U>::value == static_cast<byte_type>(major)) {
                U t;
                this->decode(t, additionalInfo);
                value = std::move(t);
                return true;
            }
            return false;
        }.template operator()<T>() || ...);

        if (!found_type_in_variant) {
            throw std::runtime_error("Invalid major type for variant");
        }
    }

    template <typename T> constexpr void decode(T &value) {
        if (reader_.empty(data_)) {
            throw std::runtime_error("Unexpected end of input");
        }

        const auto initialByte    = reader_.read(data_);
        const auto majorType      = static_cast<major_type>(static_cast<std::byte>(initialByte) >> 5);
        const auto additionalInfo = initialByte & static_cast<byte_type>(0x1F);

        if constexpr (IsString<T>) {
            constexpr auto expected_major_type = IsTextString<T> ? major_type::TextString : major_type::ByteString;
            if (majorType != expected_major_type) {
                throw std::runtime_error("Invalid major type for array or map");
            }

            decode(value, additionalInfo);
        } else if constexpr (IsRange<T>) {
            constexpr auto expected_major_type = IsMap<T> ? major_type::Map : major_type::Array;
            if (majorType != expected_major_type) {
                throw std::runtime_error("Invalid major type for array or map");
            }

            const auto length = decode_unsigned(additionalInfo);
            if constexpr (HasReserve<T>) {
                value.reserve(length);
            }
            detail::appender<T> appender_;
            for (auto i = length; i > 0; --i) {
                auto result = decode<typename T::value_type>();
                appender_(value, result);
            }
        }
        // Not range? Maybe T is a pair, e.g std::pair<cbor::tags::tag<i>, T::second_type>
        else if constexpr (IsTaggedTuple<T>) {
            static_assert(!HasCborTag<std::decay_t<decltype(T::second)>>, "The tagged type must not directly have a tag of its own");
            const auto tag = decode_unsigned(additionalInfo);
            if (tag != value.first) {
                throw std::runtime_error("Invalid tag");
            }

            if constexpr (IsAggregate<typename T::second_type>) {
                auto &&tuple = to_tuple(value.second);
                std::apply([this](auto &&...args) { (this->decode(std::forward<decltype(args)>(args)), ...); }, tuple);
            } else {
                decode(value.second);
            }
        } else if constexpr (IsAggregate<T>) {
            auto &tuple = to_tuple(value);
            std::apply([this](auto &&...args) { (this->decode(args), ...); }, tuple);
        } else if constexpr (IsTuple<T>) {
            std::apply([this](auto &&...args) { (this->decode(args), ...); }, value);
        } else {
            decode(value, additionalInfo);
        }
    }

    constexpr uint64_t decode_unsigned(byte_type additionalInfo) {
        if (additionalInfo < static_cast<byte_type>(24)) {
            return static_cast<uint64_t>(additionalInfo);
        }
        return read_unsigned(additionalInfo);
    }

    constexpr int64_t decode_integer(byte_type additionalInfo) {
        uint64_t value = decode_unsigned(additionalInfo);
        return -1 - static_cast<int64_t>(value);
    }

    constexpr auto decode_bstring(byte_type additionalInfo) {
        auto length = decode_unsigned(additionalInfo);
        if (reader_.empty(data_, length - 1)) {
            throw std::runtime_error("Unexpected end of input");
        }

        if constexpr (IsContiguous<InputBuffer>) {
            auto result = std::span<const std::byte>(reinterpret_cast<const std::byte *>(&data_[reader_.position_]), length);
            reader_.position_ += length;
            return result;
        } else {
            auto it           = std::next(reader_.position_, length);
            auto result       = subrange(reader_.position_, it);
            reader_.position_ = it;
            return binary_array_range_view{result};
        }
    }

    constexpr auto decode_text(byte_type additionalInfo) {
        auto bytes = decode_bstring(additionalInfo);
        if constexpr (IsContiguous<InputBuffer>) {
            return std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        } else {
            return char_range_view{bytes.range};
        }
    }

    constexpr auto decode_array(byte_type additionalInfo) {
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

    constexpr auto decode_map(byte_type additionalInfo) {
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

    constexpr auto decode_tag(byte_type additionalInfo) {
        auto tag  = decode_unsigned(additionalInfo);
        auto data = decode_value();
        if constexpr (IsContiguous<InputBuffer>) {
            return binary_tag_view{tag, std::get<std::span<const std::byte>>(data)};
        } else {
            return binary_tag_range_view{tag, std::get<binary_range_view<subrange>>(data).range};
        }
    }

    constexpr variant decodeSimpleOrFloat(byte_type additionalInfo) {
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

    constexpr uint64_t read_unsigned(byte_type additionalInfo) {
        switch (static_cast<uint8_t>(additionalInfo)) {
        case 24: return read_uint8();
        case 25: return read_uint16();
        case 26: return read_uint32();
        case 27: return read_uint64();
        default: throw std::runtime_error("Invalid additional info for integer");
        }
    }

    constexpr uint8_t read_uint8() {
        if (reader_.empty(data_)) {
            throw std::runtime_error("Unexpected end of input");
        }
        return static_cast<uint8_t>(reader_.read(data_));
    }

    constexpr uint16_t read_uint16() {
        if (reader_.empty(data_, 1)) {
            throw std::runtime_error("Unexpected end of input");
        }
        uint16_t result = (static_cast<uint16_t>(reader_.read(data_)) << 8) | static_cast<uint16_t>(reader_.read(data_));
        return result;
    }

    constexpr uint32_t read_uint32() {
        if (reader_.empty(data_, 3)) {
            throw std::runtime_error("Unexpected end of input");
        }
        uint32_t result = (static_cast<uint32_t>(reader_.read(data_)) << 24) | (static_cast<uint32_t>(reader_.read(data_)) << 16) |
                          (static_cast<uint32_t>(reader_.read(data_)) << 8) | static_cast<uint32_t>(reader_.read(data_));
        return result;
    }

    constexpr uint64_t read_uint64() {
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
    constexpr float16_t read_float16() {
        if (reader_.empty(data_, 1)) {
            throw std::runtime_error("Unexpected end of input");
        }

        std::uint16_t value = (static_cast<std::uint16_t>(reader_.read(data_)) << 8) | static_cast<std::uint16_t>(reader_.read(data_));
        return float16_t{value};
    }

    constexpr float read_float() {
        uint32_t bits = read_uint32();
        return std::bit_cast<float>(bits);
    }

    constexpr double read_double() {
        uint64_t bits = read_uint64();
        return std::bit_cast<double>(bits);
    }

  protected:
    inline constexpr auto read_initial_byte() {
        if (reader_.empty(data_)) {
            throw std::runtime_error("Unexpected end of input");
        }

        const auto   initialByte    = reader_.read(data_);
        const auto &&majorType      = static_cast<major_type>(static_cast<std::byte>(initialByte) >> 5);
        const auto &&additionalInfo = initialByte & static_cast<byte_type>(0x1F);

        return std::make_pair(majorType, additionalInfo);
    }

    template <typename ByteType, typename... T> constexpr bool is_valid_major(ByteType major) {
        std::cout << "major: " << static_cast<int>(major) << std::endl;

        std::cout << "sizeof...(T): " << (sizeof...(T)) << std::endl;
        auto result = (... || (major == ConceptType<ByteType, T>::value));
        std::cout << "result: " << result << std::endl;

        (fmt::print("Types: {}\n", nameof::nameof_type<T>()), ...);

        // Print all ConceptType<ByteType, T>::value
        (fmt::print("ConceptType<ByteType, T>::value: {}\n", static_cast<int>(ConceptType<ByteType, T>::value)), ...);

        (fmt::print("ConceptType<ByteType, T>::value: {}\n", (ConceptType<ByteType, T>::value) == major), ...);

        return (... || (major == ConceptType<ByteType, T>::value));
    }

    const InputBuffer          &data_;
    detail::reader<InputBuffer> reader_;
};

template <typename InputBuffer> inline auto make_decoder(InputBuffer &buffer) { return decoder<InputBuffer>(buffer); }

} // namespace cbor::tags