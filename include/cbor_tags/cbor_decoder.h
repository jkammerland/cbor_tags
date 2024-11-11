#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_concepts_checking.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_integer.h"
#include "cbor_tags/cbor_operators.h"
#include "cbor_tags/cbor_reflection.h"
#include "cbor_tags/cbor_simple.h"
#include "cbor_tags/float16_ieee754.h"

#include <cstddef>
#include <cstdint>
#include <fmt/base.h>
#include <fmt/ranges.h>
#include <iterator>
#include <map>
#include <nameof.hpp>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cbor::tags {

template <typename T> struct cbor_header_decoder;

template <typename InputBuffer = std::span<const std::byte>, template <typename> typename... Decoders>
    requires ValidCborBuffer<InputBuffer>
struct decoder : public Decoders<decoder<InputBuffer, Decoders...>>... {
    using self_t = decoder<InputBuffer, Decoders...>;
    using Decoders<self_t>::decode...;

    using size_type     = typename InputBuffer::size_type;
    using buffer_byte_t = typename InputBuffer::value_type;
    using byte          = std::byte;
    using iterator_t    = typename detail::iterator_type<InputBuffer>::type;
    using subrange      = std::ranges::subrange<iterator_t>;
    using variant       = variant_t<InputBuffer>;

    explicit decoder(const InputBuffer &data) : data_(data), reader_(data) {}

    template <typename... T> constexpr void operator()(T &&...args) { (decode(args), ...); }
    template <typename T> constexpr         operator T() { return decode<T>(); }

    template <typename T> constexpr T decode() {
        T result;
        decode(result);
        return result;
    }

    template <typename T> constexpr auto decode_value(T &value) {
        if (reader_.empty(data_)) {
            throw std::runtime_error("Unexpected end of input");
        }

        const auto initialByte    = reader_.read(data_);
        const auto additionalInfo = initialByte & static_cast<byte>(0x1F);

        decode(value, additionalInfo);
    }

    template <typename T>
        requires std::is_integral_v<T>
    constexpr void decode(T &value, major_type major, byte additionalInfo) {
        if (major == major_type::UnsignedInteger) {
            value = decode_unsigned(additionalInfo);
        } else if (major == major_type::NegativeInteger) {
            value = decode_integer(additionalInfo);
        } else {
            throw std::runtime_error("Invalid major type for integer");
        }
    }

    constexpr void decode(integer &, major_type, byte) { throw std::runtime_error("Not implemented"); }
    constexpr void decode(negative &value, major_type major, byte additionalInfo) {
        if (major != major_type::NegativeInteger) {
            throw std::runtime_error("Invalid major type for negative integer");
        }
        value = negative(decode_integer(additionalInfo));
    }

    constexpr void decode(bool &value, major_type, byte additionalInfo) {
        if (additionalInfo == static_cast<byte>(20)) {
            value = false;
        } else if (additionalInfo == static_cast<byte>(21)) {
            value = true;
        } else {
            throw std::runtime_error("Invalid additional info for boolean");
        }
    }

    constexpr void decode(std::nullptr_t &value, major_type, byte additionalInfo) {
        if (additionalInfo != static_cast<byte>(22)) {
            throw std::runtime_error("Invalid additional info for null");
        }
        value = nullptr;
    }

    constexpr void decode(float16_t &value, major_type, byte additionalInfo) {
        if (additionalInfo != static_cast<byte>(25)) {
            throw std::runtime_error("Invalid additional info for float16");
        }
        value = read_float16();
    }

    constexpr void decode(float &value, major_type, byte additionalInfo) {
        if (additionalInfo != static_cast<byte>(26)) {
            throw std::runtime_error("Invalid additional info for float");
        }
        value = read_float();
    }

    constexpr void decode(double &value, major_type, byte additionalInfo) {
        if (additionalInfo != static_cast<byte>(27)) {
            throw std::runtime_error("Invalid additional info for double");
        }
        value = read_double();
    }

    template <IsBinaryString T> constexpr void decode(T &t, major_type major, byte additionalInfo) {
        if (major == major_type::ByteString) {
            auto bstring = decode_bstring(additionalInfo);
            t            = T(bstring.begin(), bstring.end());
        } else {
            throw std::runtime_error("Invalid major type for binary string");
        }
    }

    template <IsTextString T> constexpr void decode(T &t, major_type major, byte additionalInfo) {
        if (major == major_type::TextString) {
            t = decode_text(additionalInfo);
        } else {
            throw std::runtime_error("Invalid major type for text string");
        }
    }

    template <IsRangeOfCborValues T> constexpr void decode(T &t, major_type major, byte additionalInfo) {
        if (major == major_type::Array || major == major_type::Map) {
            const auto length = decode_unsigned(additionalInfo);
            if constexpr (HasReserve<T>) {
                t.reserve(length);
            }
            detail::appender<T> appender_;
            for (auto i = length; i > 0; --i) {
                auto result = decode_value();
                appender_(t, result);
            }
        } else {
            throw std::runtime_error("Invalid major type for array or map");
        }
    }

    template <std::uint64_t N> constexpr void decode(tag<N>, major_type, byte) {}

    template <IsTag T> constexpr void decode(T &t, major_type major, byte additionalInfo) {
        if (!(major == major_type::Tag)) {
            throw std::runtime_error("Invalid major type for tagged object");
        }

        auto tag = decode_unsigned(additionalInfo);

        if constexpr (HasCborTag<T>) {
            if (tag != T::cbor_tag) {
                throw std::runtime_error("Invalid tag for tagged object");
            }
        } else if constexpr (IsTaggedTuple<T>) {
            if (tag != std::get<0>(t)) {
                throw std::runtime_error("Invalid tag for tagged object");
            }
        }

        std::apply([this](auto &&...args) { (this->decode(args), ...); }, t);
    }

    template <IsAggregate T> constexpr void decode(T &value) {
        const auto &tuple = to_tuple(value);
        std::apply([this](auto &&...args) { (this->decode(args), ...); }, tuple);
    }

    constexpr void decode(std::string &value, major_type, byte additionalInfo) { value = std::string(decode_text(additionalInfo)); }

    constexpr void decode(std::string_view &value, major_type, byte additionalInfo) { value = decode_text(additionalInfo); }

    constexpr void decode(binary_array_view &value, major_type, byte additionalInfo) { value = decode_array(additionalInfo); }

    constexpr void decode(binary_map_view &value, major_type, byte additionalInfo) { value = decode_map(additionalInfo); }

    constexpr void decode(binary_tag_view &value, major_type, byte additionalInfo) { value = decode_tag(additionalInfo); }

    constexpr void decode(char_range_view<subrange> &value, major_type, byte additionalInfo) { value = decode_text(additionalInfo); }

    constexpr void decode(binary_range_view<subrange> &value, major_type, byte additionalInfo) { value = decode_bstring(additionalInfo); }

    constexpr void decode(binary_array_range_view<subrange> &value, major_type, byte additionalInfo) {
        value = decode_array(additionalInfo);
    }

    constexpr void decode(binary_map_range_view<subrange> &value, major_type, byte additionalInfo) { value = decode_map(additionalInfo); }

    constexpr void decode(binary_tag_range_view<subrange> &value, major_type, byte additionalInfo) { value = decode_tag(additionalInfo); }

    constexpr void decode(variant &value, major_type, byte additionalInfo) {
        if (additionalInfo == static_cast<byte>(20) || additionalInfo == static_cast<byte>(21)) {
            value = additionalInfo == static_cast<byte>(21);
        } else if (additionalInfo == static_cast<byte>(22)) {
            value = nullptr;
        } else if (additionalInfo >= static_cast<byte>(25) && additionalInfo <= static_cast<byte>(27)) {
            switch (additionalInfo) {
            case static_cast<byte>(25): value = read_float16(); break;
            case static_cast<byte>(26): value = read_float(); break;
            case static_cast<byte>(27): value = read_double(); break;
            default: throw std::runtime_error("Invalid additional info for float");
            }
        } else {
            value = decode_value();
        }
    }

    template <typename T> constexpr void decode(std::optional<T> &value, major_type major, byte additionalInfo) {
        if (additionalInfo == static_cast<byte>(22)) {
            value = std::nullopt;
        } else {
            using value_type = std::decay_t<T>;
            value_type t;
            decode(t, major, additionalInfo);
            value = std::move(t);
        }
    }

    template <IsCborMajor... T> constexpr void decode(std::variant<T...> &value, major_type major, byte additionalInfo) {
        if (!is_valid_major<decltype(major), T...>(major)) {
            throw std::runtime_error("Invalid major type for variant");
        }

        // Print all types
        // fmt::print("Types: {}\n", fmt::join({nameof::nameof_type<T>()...}, ", "));

        auto try_decode = [this, major, additionalInfo, &value]<typename U>() -> bool {
            // // fmt::print("Nameof U type: {}\n", nameof::nameof_type<U>());
            // bool condition =
            //     (ConceptType<byte, U>::value != static_cast<byte>(major)) && !(IsSigned<U> && (major == major_type::UnsignedInteger));
            // bool condition2 = !(IsSigned<U> && (major == major_type::UnsignedInteger));
            // bool condition3 = major == major_type::UnsignedInteger;
            // fmt::print("TEST: {}, {}, {}\n", condition, condition2, condition3);

            if constexpr (IsSigned<U>) {
                if (major > major_type::NegativeInteger) {
                    return false;
                }
            } else {
                if (ConceptType<byte, U>::value != static_cast<byte>(major)) {
                    return false;
                }
            }

            if constexpr (IsSimple<U>) {
                // fmt::print("Simple type: {}\n", nameof::nameof_type<U>());
                if constexpr (IsFloat16<U>) {
                    if (additionalInfo != static_cast<byte>(25)) {
                        return false;
                    }
                    float16_t v;
                    decode(v, major, additionalInfo);
                    value = v;
                } else if constexpr (IsFloat32<U>) {
                    if (additionalInfo != static_cast<byte>(26)) {
                        return false;
                    }
                    float v;
                    decode(v, major, additionalInfo);
                    value = v;
                } else if constexpr (IsFloat64<U>) {
                    if (additionalInfo != static_cast<byte>(27)) {
                        return false;
                    }
                    double v;
                    decode(v, major, additionalInfo);
                    value = v;
                } else if constexpr (IsBool<U>) {
                    if (additionalInfo != static_cast<byte>(20) && additionalInfo != static_cast<byte>(21)) {
                        return false;
                    }
                    bool v;
                    decode(v, major, additionalInfo);
                    value = v;
                } else if constexpr (IsNull<U>) {
                    if (additionalInfo != static_cast<byte>(22)) {
                        return false;
                    }
                    value = nullptr;
                } else {
                    return false;
                }

                return true;
            }

            U decoded_value;
            this->decode(decoded_value, major, additionalInfo);
            value = std::move(decoded_value);
            return true;
        };

        bool found = (try_decode.template operator()<T>() || ...);
        if (!found) {
            throw std::runtime_error("Invalid major type for variant");
        }
    }
    template <typename T> constexpr void decode(T &value) {
        if (reader_.empty(data_)) {
            throw std::runtime_error("Unexpected end of input");
        }

        const auto [majorType, additionalInfo] = read_initial_byte();

        if constexpr (IsString<T>) {
            constexpr auto expected_major_type = IsTextString<T> ? major_type::TextString : major_type::ByteString;
            if (majorType != expected_major_type) {
                throw std::runtime_error("Invalid major type for array or map");
            }

            decode(value, majorType, additionalInfo);
        } else if constexpr (IsMap<T>) {
            constexpr auto expected_major_type = major_type::Map;
            if (majorType != expected_major_type) {
                throw std::runtime_error("Invalid major type for map");
            }

            const auto length = decode_unsigned(additionalInfo);
            for (auto i = length; i > 0; --i) {
                auto key = decode<typename T::key_type>();
                auto val = decode<typename T::mapped_type>();
                value.insert_or_assign(key, val);
            }
        } else if constexpr (IsRangeOfCborValues<T>) {
            constexpr auto expected_major_type = major_type::Array;
            if (majorType != expected_major_type) {
                throw std::runtime_error("Invalid major type for array");
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
            decode(value, majorType, additionalInfo);
        }
    }

    constexpr uint64_t decode_unsigned(byte additionalInfo) {
        if (additionalInfo < static_cast<byte>(24)) {
            return static_cast<uint64_t>(additionalInfo);
        }
        return read_unsigned(additionalInfo);
    }

    constexpr int64_t decode_integer(byte additionalInfo) {
        uint64_t value = decode_unsigned(additionalInfo);
        return -1 - static_cast<int64_t>(value);
    }

    constexpr auto decode_bstring(byte additionalInfo) {
        auto length = decode_unsigned(additionalInfo);
        if (reader_.empty(data_, length - 1)) {
            throw std::runtime_error("Unexpected end of input");
        }

        if constexpr (IsContiguous<InputBuffer>) {
            auto result = std::span<const byte>(reinterpret_cast<const byte *>(&data_[reader_.position_]), length);
            reader_.position_ += length;
            return result;
        } else {
            auto it           = std::next(reader_.position_, length);
            auto result       = subrange(reader_.position_, it);
            reader_.position_ = it;
            return binary_array_range_view{result};
        }
    }

    constexpr auto decode_text(byte additionalInfo) {
        auto bytes = decode_bstring(additionalInfo);
        if constexpr (IsContiguous<InputBuffer>) {
            return std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        } else {
            return char_range_view{bytes.range};
        }
    }

    constexpr auto decode_array(byte additionalInfo) {
        auto length   = decode_unsigned(additionalInfo);
        auto startPos = reader_.position_;
        for (size_t i = 0; i < length; ++i) {
            decode_value();
        }

        if constexpr (IsContiguous<InputBuffer>) {
            return binary_array_view{std::span<const byte>(reinterpret_cast<const byte *>(&data_[startPos]), reader_.position_ - startPos)};
        } else {
            return binary_array_range_view{std::ranges::subrange(startPos, reader_.position_)};
        }
    }

    constexpr auto decode_map(byte additionalInfo) {
        size_t length   = decode_unsigned(additionalInfo);
        auto   startPos = reader_.position_;
        for (size_t i = 0; i < length; ++i) {
            decode_value(); // key
            decode_value(); // value
        }

        if constexpr (IsContiguous<InputBuffer>) {
            return binary_map_view{std::span<const byte>(reinterpret_cast<const byte *>(&data_[startPos]), reader_.position_ - startPos)};
        } else {
            return binary_map_range_view{std::ranges::subrange(startPos, reader_.position_)};
        }
    }

    constexpr auto decode_tag(byte additionalInfo) {
        auto tag  = decode_unsigned(additionalInfo);
        auto data = decode_value();
        if constexpr (IsContiguous<InputBuffer>) {
            return binary_tag_view{tag, std::get<std::span<const byte>>(data)};
        } else {
            return binary_tag_range_view{tag, std::get<binary_range_view<subrange>>(data).range};
        }
    }

    constexpr variant decodeSimpleOrFloat(byte additionalInfo) {
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

    constexpr uint64_t read_unsigned(byte additionalInfo) {
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

    inline constexpr auto read_initial_byte() {
        if (reader_.empty(data_)) {
            throw std::runtime_error("Unexpected end of input");
        }

        const auto   initialByte    = reader_.read(data_);
        const auto &&majorType      = static_cast<major_type>(static_cast<byte>(initialByte) >> 5);
        const auto &&additionalInfo = initialByte & static_cast<byte>(0x1F);

        return std::make_pair(majorType, additionalInfo);
    }

    template <typename ByteType, typename... T> constexpr bool is_valid_major(ByteType major) {
        // fmt::print("Major: {} - [ ", static_cast<int>(major));
        // (fmt::print("{} ", static_cast<int>(ConceptType<ByteType, T>::value)), ...);
        // fmt::print("]\n");
        // // Check any Signed?
        // (fmt::print("IsSigned: {}\n", IsSigned<unwrap_type_t<T>>), ...);
        // bool test = (... || ((major == ConceptType<ByteType, unwrap_type_t<T>>::value) ||
        //  (IsSigned<unwrap_type_t<T>> && (major == static_cast<ByteType>(0) || major == static_cast<ByteType>(1)))));
        // fmt::print("Test: {}\n", test);

        return (... || ((major == ConceptType<ByteType, unwrap_type_t<T>>::value) ||
                        (IsSigned<unwrap_type_t<T>> && (major == static_cast<ByteType>(0) || major == static_cast<ByteType>(1)))));
    }

    // Variadic friends only in c++26, must be public
    const InputBuffer          &data_;
    detail::reader<InputBuffer> reader_;
};

template <typename T> struct cbor_header_decoder : crtp_base<T> {

    constexpr auto get_and_validate_header(major_type expectedMajorType) {
        auto [initialByte, additionalInfo] = this->underlying().read_initial_byte();
        if (initialByte != expectedMajorType) {
            throw std::runtime_error("Invalid major type");
        }
        return additionalInfo;
    }

    constexpr auto validate_size(major_type expectedMajor, std::uint64_t expectedSize) {
        auto additionalInfo = get_and_validate_header(expectedMajor);
        auto size           = this->underlying().decode_unsigned(additionalInfo);
        if (size != expectedSize) {
            throw std::runtime_error("Invalid container size");
        }
    }

    constexpr void decode(as_array value) { validate_size(major_type::Array, value.size_); }
    constexpr void decode(as_map value) { validate_size(major_type::Map, value.size_); }
};

template <typename InputBuffer> inline auto make_decoder(InputBuffer &buffer) { return decoder<InputBuffer, cbor_header_decoder>(buffer); }

} // namespace cbor::tags