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

#include <bit>
#include <cstddef>
#include <cstdint>
// #include <fmt/base.h>
// #include <fmt/ranges.h>
#include <iterator>
// #include <magic_enum/magic_enum.hpp>
#include <map>
// #include <nameof.hpp>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cbor::tags {

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

    template <IsSigned T> constexpr void decode(T &value, major_type major, byte additionalInfo) {
        if (major == major_type::UnsignedInteger) {
            value = decode_unsigned(additionalInfo);
        } else if (major == major_type::NegativeInteger) {
            value = decode_integer(additionalInfo);
        } else {
            throw std::runtime_error("Invalid major type for integer");
        }
    }

    template <IsUnsigned T> constexpr void decode(T &value, major_type major, byte additionalInfo) {
        if (major != major_type::UnsignedInteger) {
            throw std::runtime_error("Invalid major type for unsigned integer");
        }
        value = decode_unsigned(additionalInfo);
    }

    constexpr void decode(negative &value, major_type major, byte additionalInfo) {
        if (major != major_type::NegativeInteger) {
            throw std::runtime_error("Invalid major type for negative integer");
        }
        value = negative(decode_unsigned(additionalInfo) + 1);
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

    template <IsRangeOfCborValues T> constexpr void decode(T &value, major_type major, byte additionalInfo) {
        if (major != major_type::Array && major != major_type::Map) {
            throw std::runtime_error("Invalid major type for range of cbor values");
        }

        const auto length = decode_unsigned(additionalInfo);
        if constexpr (HasReserve<T>) {
            value.reserve(length);
        }
        detail::appender<T> appender_;
        for (auto i = length; i > 0; --i) {
            if constexpr (IsMap<T>) {
                using value_type = std::pair<typename T::key_type, typename T::mapped_type>;
                value_type result;
                decode(result);
                appender_(value, result);
            } else {
                using value_type = typename T::value_type;
                value_type result;
                decode(result);
                appender_(value, result);
            }
        }
    }

    template <std::uint64_t N> constexpr void decode(static_tag<N>, major_type major, byte additionalInfo) {
        if (major != major_type::Tag) {
            throw std::runtime_error("Invalid major type for tag");
        }
        if (decode_unsigned(additionalInfo) != N) {
            throw std::runtime_error("Invalid tag value");
        }
    }

    template <std::uint64_t N> constexpr void decode(static_tag<N> value) {
        auto [major, additionalInfo] = read_initial_byte();
        decode(value, major, additionalInfo);
    }

    template <IsUnsigned T> constexpr void decode(dynamic_tag<T> &value, major_type major, byte additionalInfo) {
        if (major != major_type::Tag) {
            throw std::runtime_error("Invalid major type for dynamic tag");
        }

        // TODO: FIX NARROWING PROBLEM!!!
        value = dynamic_tag<T>{decode_unsigned(additionalInfo)};
    }

    // template <HasInlineTag T> constexpr void decode(T &value) {
    //     decode(static_tag<T::cbor_tag>{});
    //     std::apply([this](auto &&...args) { (this->decode(args), ...); }, to_tuple(value));
    // }

    template <IsUnsigned T> constexpr void decode(dynamic_tag<T> &value) {
        auto [major, additionalInfo] = read_initial_byte();
        decode(value, major, additionalInfo);
    }

    template <IsTaggedTuple T> constexpr void decode(T &t, major_type major, byte additionalInfo) {
        if (major != major_type::Tag) {
            throw std::runtime_error("Invalid major type for tagged object");
        }

        auto tag = decode_unsigned(additionalInfo);

        if (tag != std::get<0>(t)) {
            throw std::runtime_error("Invalid tag for tagged object");
        }

        std::apply([this](auto &&...args) { (this->decode(args), ...); }, detail::tuple_tail(t));
    }

    template <IsAggregate T> constexpr void decode(T &value) {
        const auto &tuple = to_tuple(value);

        if constexpr (HasInlineTag<T>) {
            decode(static_tag<T::cbor_tag>{});
        }
        std::apply([this](auto &&...args) { (this->decode(args), ...); }, tuple);
    }

    template <IsAggregate T> constexpr void decode(T &value, major_type major, byte additionalInfo) {
        if (major != major_type::Tag) {
            throw std::runtime_error("Invalid major type for tagged object");
        }

        const auto &tuple = to_tuple(value);
        auto        tag   = decode_unsigned(additionalInfo);
        if constexpr (HasInlineTag<T>) {
            if (tag != T::cbor_tag) {
                throw std::runtime_error("Invalid tag for tagged object");
            }
            std::apply([this](auto &&...args) { (this->decode(args), ...); }, tuple);
        } else {
            if constexpr (HasStaticTag<T>) {
                if (tag != std::get<0>(tuple)) {
                    throw std::runtime_error("Invalid tag for tagged object");
                }
            } else {
                std::get<0>(tuple) = tag;
            }

            std::apply([this](auto &&...args) { (this->decode(args), ...); }, detail::tuple_tail(tuple));
        }
    }

    template <IsUntaggedTuple T> constexpr void decode(T &value) {
        std::apply([this](auto &&...args) { (this->decode(args), ...); }, value);
    }

    constexpr void decode(bool &value, major_type major, byte additionalInfo) {
        if (major != major_type::Simple) {
            throw std::runtime_error("Invalid major type for boolean");
        }
        if (additionalInfo == static_cast<byte>(20)) {
            value = false;
        } else if (additionalInfo == static_cast<byte>(21)) {
            value = true;
        } else {
            throw std::runtime_error("Invalid additional info for boolean");
        }
    }

    constexpr void decode(std::nullptr_t &value, major_type major, byte additionalInfo) {
        if (major != major_type::Simple || additionalInfo != static_cast<byte>(22)) {
            throw std::runtime_error("Invalid additional info for null");
        }
        value = nullptr;
    }

    constexpr void decode(float16_t &value, major_type major, byte additionalInfo) {
        if (major != major_type::Simple || additionalInfo != static_cast<byte>(25)) {
            throw std::runtime_error("Invalid additional info for float16");
        }
        value = read_float16();
    }

    constexpr void decode(float &value, major_type major, byte additionalInfo) {
        if (major != major_type::Simple || additionalInfo != static_cast<byte>(26)) {
            throw std::runtime_error("Invalid additional info for float");
        }
        value = read_float();
    }

    constexpr void decode(double &value, major_type major, byte additionalInfo) {
        if (major != major_type::Simple || additionalInfo != static_cast<byte>(27)) {
            throw std::runtime_error("Invalid additional info for double");
        }
        value = read_double();
    }

    constexpr void decode(std::string &value, major_type, byte additionalInfo) { value = std::string(decode_text(additionalInfo)); }

    constexpr void decode(std::string_view &value, major_type, byte additionalInfo) { value = decode_text(additionalInfo); }

    template <IsCborMajor T> constexpr void decode(std::optional<T> &value, major_type major, byte additionalInfo) {
        if (additionalInfo == static_cast<byte>(22)) {
            value = std::nullopt;
        } else {
            using value_type = std::remove_cvref_t<T>;
            value_type t;
            decode(t, major, additionalInfo);
            value = std::move(t);
        }
    }

    template <IsCborMajor... T> constexpr void decode(std::variant<T...> &value, major_type major, byte additionalInfo) {
        auto try_decode = [this, major, additionalInfo, &value]<typename U>() -> bool {
            if (!is_valid_major<decltype(major), U>(major)) {
                return false;
            }

            if constexpr (IsSimple<U>) {
                if (!compare_simple_value<U>(additionalInfo)) {
                    return false;
                }
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

        // fmt::print("decoding {}, major: {}, additional info: {}\n", nameof::nameof_short_type<T>(), magic_enum::enum_name(majorType),
        //            additionalInfo);

        decode(value, majorType, additionalInfo);
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

    // Variadic friends only in c++26, must be public
    const InputBuffer          &data_;
    detail::reader<InputBuffer> reader_;
};

template <typename T> struct cbor_header_decoder {
    constexpr auto get_and_validate_header(major_type expectedMajorType) {
        auto [initialByte, additionalInfo] = detail::underlying<T>(this).read_initial_byte();
        if (initialByte != expectedMajorType) {
            throw std::runtime_error("Invalid major type");
        }
        return additionalInfo;
    }

    constexpr auto validate_size(major_type expectedMajor, std::uint64_t expectedSize) {
        auto additionalInfo = get_and_validate_header(expectedMajor);
        auto size           = detail::underlying<T>(this).decode_unsigned(additionalInfo);
        if (size != expectedSize) {
            throw std::runtime_error("Invalid container size");
        }
    }

    constexpr void decode(as_array value) { validate_size(major_type::Array, value.size_); }
    constexpr void decode(as_map value) { validate_size(major_type::Map, value.size_); }
};

template <typename T> struct enum_decoder {

    template <IsEnum U> constexpr void decode(U &value, major_type major, std::byte additionalInfo) {
        using underlying_type = std::underlying_type_t<U>;
        if constexpr (IsSigned<underlying_type>) {
            if (major > major_type::NegativeInteger) {
                throw std::runtime_error("Invalid major type for enum");
            }
        } else if constexpr (IsUnsigned<underlying_type>) {
            if (major != major_type::UnsignedInteger) {
                throw std::runtime_error("Invalid major type for enum");
            }
        } else {
            throw std::runtime_error("Invalid enum type");
        }

        underlying_type result;
        detail::underlying<T>(this).decode(result, major, additionalInfo);
        value = static_cast<U>(result);
    }

    template <IsEnum U> constexpr void decode(U &value) {
        auto [major, additionalInfo] = detail::underlying<T>(this).read_initial_byte();
        decode(value, major, additionalInfo);
    }
};

template <typename InputBuffer> inline auto make_decoder(InputBuffer &buffer) {
    return decoder<InputBuffer, cbor_header_decoder, enum_decoder>(buffer);
}

} // namespace cbor::tags