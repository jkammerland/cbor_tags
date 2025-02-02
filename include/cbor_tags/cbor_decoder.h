#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_concepts_checking.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_integer.h"
#include "cbor_tags/cbor_reflection.h"
#include "cbor_tags/float16_ieee754.h"

#include <bit>
#include <cstddef>
#include <cstdint>
// #include <fmt/base.h>
// #include <fmt/ranges.h>
#include <exception>
// #include <fmt/base.h>
#include <iterator>
// #include <magic_enum/magic_enum.hpp>
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

template <typename InputBuffer, IsOptions Options, template <typename> typename... Decoders>
    requires ValidCborBuffer<InputBuffer>
struct decoder : public Decoders<decoder<InputBuffer, Options, Decoders...>>... {
    using self_t = decoder<InputBuffer, Options, Decoders...>;
    using Decoders<self_t>::decode...;

    using size_type       = typename InputBuffer::size_type;
    using buffer_byte_t   = typename InputBuffer::value_type;
    using byte            = std::byte;
    using iterator_t      = typename detail::iterator_type<InputBuffer>::type;
    using subrange        = std::ranges::subrange<iterator_t>;
    using expected_type   = typename Options::return_type;
    using unexpected_type = typename Options::error_type;
    using options         = Options;

    explicit decoder(const InputBuffer &data) : data_(data), reader_(data) {}

    template <typename... T> expected_type operator()(T &&...args) noexcept {
        try {
            status_collector<self_t> collect_status{*this};

            auto success = (collect_status(args) && ...);

            if (!success) {
                return unexpected<decltype(collect_status.result)>(collect_status.result);
            }
            return expected_type{};
        } catch (const std::bad_alloc &) { return unexpected<status_code>(status_code::out_of_memory); } catch (const std::exception &) {
            // std::rethrow_exception(std::current_exception());   // for debugging, this handling is TODO!
            return unexpected<status_code>(status_code::error); // placeholder
        }
    }

    template <IsSigned T> constexpr status_code decode(T &value, major_type major, byte additionalInfo) {
        if (major == major_type::UnsignedInteger) {
            value = decode_unsigned(additionalInfo);
        } else if (major == major_type::NegativeInteger) {
            value = decode_integer(additionalInfo);
        } else {
            // throw std::runtime_error("Invalid major type for integer");
            return status_code::invalid_major_type_for_integer;
        }

        return status_code::success;
    }

    template <IsUnsigned T> constexpr status_code decode(T &value, major_type major, byte additionalInfo) {
        if (major != major_type::UnsignedInteger) {
            // throw std::runtime_error("Invalid major type for unsigned integer");
            return status_code::invalid_major_type_for_unsigned_integer;
        }
        value = decode_unsigned(additionalInfo);

        return status_code::success;
    }

    constexpr status_code decode(negative &value, major_type major, byte additionalInfo) {
        if (major != major_type::NegativeInteger) {
            // throw std::runtime_error("Invalid major type for negative integer");
            return status_code::invalid_major_type_for_negative_integer;
        }
        value = negative(decode_unsigned(additionalInfo) + 1);

        return status_code::success;
    }

    template <IsBinaryString T> constexpr status_code decode(T &t, major_type major, byte additionalInfo) {
        if (major == major_type::ByteString) {
            auto bstring = decode_bstring(additionalInfo);
            t            = T(bstring.begin(), bstring.end());
        } else {
            // throw std::runtime_error("Invalid major type for binary string");
            return status_code::invalid_major_type_for_binary_string;
        }

        return status_code::success;
    }

    template <IsTextString T> constexpr status_code decode(T &t, major_type major, byte additionalInfo) {
        if (major == major_type::TextString) {
            t = decode_text(additionalInfo);
        } else {
            // throw std::runtime_error("Invalid major type for text string");
            return status_code::invalid_major_type_for_text_string;
        }

        return status_code::success;
    }

    template <IsRangeOfCborValues T> constexpr status_code decode(T &value, major_type major, byte additionalInfo) {
        if constexpr (IsMap<T>) {
            if (major != major_type::Map) {
                // throw std::runtime_error("Not map");
                return status_code::invalid_major_type_for_map;
            }
        } else {
            if (major != major_type::Array) {
                // throw std::runtime_error("Not array");
                return status_code::invalid_major_type_for_array;
            }
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
                auto &[key, mapped_value] = result;
                auto status               = decode(key);
                status                    = status == status_code::success ? decode(mapped_value) : status;
                if (status != status_code::success) {
                    return status;
                }
                appender_(value, result);
            } else {
                using value_type = typename T::value_type;
                value_type result;
                auto       status = decode(result);
                if (status != status_code::success) {
                    return status;
                }
                appender_(value, result);
            }
        }

        return status_code::success;
    }

    template <std::uint64_t N> constexpr status_code decode(static_tag<N>, major_type major, byte additionalInfo) {
        if (major != major_type::Tag) {
            // throw std::runtime_error("Invalid major type for tag");
            return status_code::invalid_major_type_for_tag;
        }
        if (decode_unsigned(additionalInfo) != N) {
            // throw std::runtime_error("Invalid tag value");
            return status_code::invalid_tag_value;
        }
        return status_code::success;
    }

    template <std::uint64_t N> constexpr status_code decode(static_tag<N> value) {
        auto [major, additionalInfo] = read_initial_byte();
        return decode(value, major, additionalInfo);
    }

    template <IsUnsigned T> constexpr status_code decode(dynamic_tag<T> &value, major_type major, byte additionalInfo) {
        if (major != major_type::Tag) {
            // throw std::runtime_error("Invalid major type for dynamic tag");
            return status_code::invalid_major_type_for_tag;
        }

        auto decoded_value = dynamic_tag<T>{decode_unsigned(additionalInfo)};
        if (decoded_value.value != value.value) {
            // throw std::runtime_error("Invalid tag value for dynamic tag");
            return status_code::invalid_tag_value;
        }

        return status_code::success;
    }

    template <IsUnsigned T> constexpr status_code decode(dynamic_tag<T> &value) {
        auto [major, additionalInfo] = read_initial_byte();
        return decode(value, major, additionalInfo);
    }

    template <IsTaggedTuple T> constexpr status_code decode(T &t, major_type major, byte additionalInfo) {
        if (major != major_type::Tag) {
            // throw std::runtime_error("Invalid major type for tagged object");
            return status_code::invalid_major_type_for_tag;
        }

        auto tag = decode_unsigned(additionalInfo);

        if (tag != std::get<0>(t)) {
            // throw std::runtime_error("Invalid tag for tagged object");
            return status_code::invalid_tag_value;
        }

        auto array_header_status = this->decode_wrapped_group(detail::tuple_tail(t));
        if (array_header_status != status_code::success) {
            return array_header_status;
        }
        return std::apply([this](auto &&...args) { return this->applier(std::forward<decltype(args)>(args)...); }, detail::tuple_tail(t));
    }

    template <IsAggregate T> constexpr status_code decode(T &value) {
        const auto &tuple = to_tuple(value);

        auto result = status_code::success;
        if constexpr (HasInlineTag<T>) {
            result = decode(static_tag<T::cbor_tag>{});
        } else if constexpr (IsTag<T>) {
            result = decode(std::get<0>(tuple));
        }
        // ...
        if (result != status_code::success) {
            return result;
        }

        auto array_header_status = status_code::success;
        if constexpr (HasInlineTag<T> || !IsTag<T>) {
            array_header_status = this->decode_wrapped_group(tuple);
        } else {
            array_header_status = this->decode_wrapped_group(detail::tuple_tail(tuple));
        }
        // ...
        if (array_header_status != status_code::success) {
            return array_header_status;
        }

        if constexpr (HasInlineTag<T> || !IsTag<T>) {
            return std::apply([this](auto &&...args) { return this->applier(std::forward<decltype(args)>(args)...); }, tuple);
        } else {
            return std::apply([this](auto &&...args) { return this->applier(std::forward<decltype(args)>(args)...); },
                              detail::tuple_tail(tuple));
        }
    }

    template <IsAggregate T> constexpr status_code decode(T &value, major_type major, byte additionalInfo) {
        if (major != major_type::Tag) {
            // throw std::runtime_error("Invalid major type for tagged object");
            return status_code::invalid_major_type_for_tag;
        }

        const auto &tuple = to_tuple(value);
        auto        tag   = decode_unsigned(additionalInfo);
        if constexpr (HasInlineTag<T>) {
            if (tag != T::cbor_tag) {
                // throw std::runtime_error("Invalid tag for tagged object");
                return status_code::invalid_tag_value;
            }
            auto array_header_status = this->decode_wrapped_group(tuple);
            if (array_header_status != status_code::success) {
                return array_header_status;
            }
            return std::apply([this](auto &&...args) { return this->applier(std::forward<decltype(args)>(args)...); }, tuple);
        } else {
            if (tag != std::get<0>(tuple)) {
                // throw std::runtime_error("Invalid tag for tagged object");
                return status_code::invalid_tag_value;
            }
            auto array_header_status = this->decode_wrapped_group(detail::tuple_tail(tuple));
            if (array_header_status != status_code::success) {
                return array_header_status;
            }
            return std::apply([this](auto &&...args) { return this->applier(std::forward<decltype(args)>(args)...); },
                              detail::tuple_tail(tuple));
        }
    }

    template <IsUntaggedTuple T> constexpr status_code decode(T &value) {
        auto array_header_status = this->decode_wrapped_group(value);
        if (array_header_status != status_code::success) {
            return array_header_status;
        }
        return std::apply([this](auto &&...args) { return this->applier(std::forward<decltype(args)>(args)...); }, value);
    }

    constexpr status_code decode(bool &value, major_type major, byte additionalInfo) {
        if (major != major_type::Simple) {
            // throw std::runtime_error("Invalid major type for boolean");
            return status_code::invalid_major_type_for_simple;
        }
        if (additionalInfo == static_cast<byte>(20)) {
            value = false;
        } else if (additionalInfo == static_cast<byte>(21)) {
            value = true;
        } else {
            // throw std::runtime_error("Invalid additional info for boolean");
            return status_code::invalid_tag_for_simple;
        }
        return status_code::success;
    }

    constexpr status_code decode(std::nullptr_t &value, major_type major, byte additionalInfo) {
        if (major != major_type::Simple) {
            // throw std::runtime_error("Invalid additional info for null");
            return status_code::invalid_major_type_for_simple;
        } else if (additionalInfo != static_cast<byte>(22)) {
            // throw std::runtime_error("Invalid additional info for null");
            return status_code::invalid_tag_for_simple;
        }
        value = nullptr;
        return status_code::success;
    }

    constexpr status_code decode(float16_t &value, major_type major, byte additionalInfo) {
        if (major != major_type::Simple) {
            return status_code::invalid_major_type_for_simple;
        } else if (additionalInfo != static_cast<byte>(25)) {
            return status_code::invalid_tag_for_simple;
        }
        value = read_float16();
        return status_code::success;
    }

    constexpr status_code decode(float &value, major_type major, byte additionalInfo) {
        if (major != major_type::Simple) {
            return status_code::invalid_major_type_for_simple;
        } else if (additionalInfo != static_cast<byte>(26)) {
            return status_code::invalid_tag_for_simple;
        }
        value = read_float();
        return status_code::success;
    }

    constexpr status_code decode(double &value, major_type major, byte additionalInfo) {
        if (major != major_type::Simple) {
            return status_code::invalid_major_type_for_simple;
        } else if (additionalInfo != static_cast<byte>(27)) {
            return status_code::invalid_tag_for_simple;
        }
        value = read_double();
        return status_code::success;
    }

    constexpr status_code decode(std::string &value, major_type major, byte additionalInfo) {
        if (major != major_type::TextString) {
            return status_code::invalid_major_type_for_text_string;
        }
        value = std::string(decode_text(additionalInfo));
        return status_code::success;
    }

    constexpr status_code decode(std::string_view &value, major_type major, byte additionalInfo) {
        if (major != major_type::TextString) {
            return status_code::invalid_major_type_for_text_string;
        }
        value = decode_text(additionalInfo);
        return status_code::success;
    }

    template <IsCborMajor T> constexpr status_code decode(std::optional<T> &value, major_type major, byte additionalInfo) {
        if (major == major_type::Simple && additionalInfo == static_cast<byte>(22)) {
            value = std::nullopt;
            return status_code::success;
        } else {
            using value_type = std::remove_cvref_t<T>;
            value_type t;
            auto       result = decode(t, major, additionalInfo);
            if (result == status_code::success) {
                value = std::move(t);
            } else {
                value = std::nullopt;
            }
            return result;
        }
        return status_code::invalid_tag_for_optional;
    }

    template <IsCborMajor... T> constexpr status_code decode(std::variant<T...> &value, major_type major, byte additionalInfo) {
        using namespace detail;
        using Variant                                     = std::variant<T...>;
        constexpr auto no_ambigous_major_types_in_variant = valid_concept_mapping_v<Variant>;
        constexpr auto matching_major_types               = valid_concept_mapping_array_v<Variant>;
        static_assert(matching_major_types[MajorIndex::Unsigned] <= 1, "Multiple types match against major type 0 (unsigned integer)");
        static_assert(matching_major_types[MajorIndex::Negative] <= 1, "Multiple types match against major type 1 (negative integer)");
        static_assert(matching_major_types[MajorIndex::BStr] <= 1, "Multiple types match against major type 2 (byte string)");
        static_assert(matching_major_types[MajorIndex::TStr] <= 1, "Multiple types match against major type 3 (text string)");
        static_assert(matching_major_types[MajorIndex::Array] <= 1, "Multiple types match against major type 4 (array)");
        static_assert(matching_major_types[MajorIndex::Map] <= 1, "Multiple types match against major type 5 (map)");
        static_assert(matching_major_types[MajorIndex::Tag] <= 1, "Multiple types match against major type 6 (tag)");
        static_assert(matching_major_types[MajorIndex::SimpleValued] <= 1, "Multiple types match against major type 7 (simple)");
        static_assert(matching_major_types[MajorIndex::Boolean] <= 1, "Multiple types match against major type 7 (boolean)");
        static_assert(matching_major_types[MajorIndex::Null] <= 1, "Multiple types match against major type 7 (null)");
        static_assert(matching_major_types[MajorIndex::float16] <= 1, "Multiple types match against major type 7 (float16)");
        static_assert(matching_major_types[MajorIndex::float32] <= 1, "Multiple types match against major type 7 (float32)");
        static_assert(matching_major_types[MajorIndex::float64] <= 1, "Multiple types match against major type 7 (float64)");
        static_assert(matching_major_types[MajorIndex::DEPRECATED_Simple] == 0,
                      "Simple is deprecated, use boolean, null, float16, float32, float64, SimpleValued");
        // static_assert(matching_major_types[MajorIndex::Unmatched] == 0, "Unmatched major types in variant");
        static_assert(matching_major_types[MajorIndex::DynamicTag] == 0,
                      "Variant cannot contain dynamic tags, must be known at compile time, use as_any_tag to catch any tag");

        static_assert(no_ambigous_major_types_in_variant, "Variant has ambigous major types, if this would compile, only the first type \
                                                          (among the ambigous) would get decoded.");

        auto try_decode = [this, major, additionalInfo, &value]<typename U>() -> bool {
            if (!is_valid_major<major_type, U>(major)) {
                return false;
            }

            // fmt::print("decoding {}, major: {}, additional info: {}\n", nameof::nameof_full_type<U>(), magic_enum::enum_name(major),
            //            additionalInfo);

            U decoded_value;

            status_code result;
            if constexpr (IsSimple<U>) {
                if (!compare_simple_value<U>(additionalInfo)) {
                    return false;
                }
                result = this->decode(decoded_value, major, additionalInfo);
            } else if constexpr (IsTag<U>) {
                result = this->decode(decoded_value, major, additionalInfo);
                if (result != status_code::success) {
                    // TODO: THIS WILL LEAVE IN A BAD STATE FOR INCOMPLETE PARSING OF STRUCTS!!!

                    // Attempt reverse reader position depending on additionalInfo
                    switch (additionalInfo) {
                    case static_cast<byte>(27):
                        reader_.position_--;
                        reader_.position_--;
                        reader_.position_--;
                        reader_.position_--;
                        [[fallthrough]];
                    case static_cast<byte>(26):
                        reader_.position_--;
                        reader_.position_--;
                        [[fallthrough]];
                    case static_cast<byte>(25): reader_.position_--; [[fallthrough]];
                    case static_cast<byte>(24): reader_.position_--; [[fallthrough]];
                    default: break;
                    }
                    return false;
                }
            } else {
                result = this->decode(decoded_value, major, additionalInfo);
            }

            if (result == status_code::success) {
                value = std::move(decoded_value);
                return true;
            } else {
                return false;
            }
        };

        try {
            bool found = (try_decode.template operator()<T>() || ...);
            if (!found) {
                // throw std::runtime_error("Invalid major type for variant");
                return status_code::no_matching_tag_value_in_variant;
            }
        } catch (...) { std::rethrow_exception(std::current_exception()); }
        return status_code::success;
    }

    constexpr status_code decode(as_text_any &value, major_type major, byte additionalInfo) {
        if (major != major_type::TextString) {
            // throw std::runtime_error("Invalid major type for text string");
            return status_code::invalid_major_type_for_text_string;
        }
        value.size = decode_unsigned(additionalInfo);

        if constexpr (IsContiguous<InputBuffer>) {
            reader_.position_ += value.size;
        } else {
            reader_.position_ = std::next(reader_.position_, value.size);
        }

        return status_code::success;
    }
    constexpr status_code decode(as_bstr_any &value, major_type major, byte additionalInfo) {
        if (major != major_type::ByteString) {
            // throw std::runtime_error("Invalid major type for binary string");
            return status_code::invalid_major_type_for_binary_string;
        }
        value.size = decode_unsigned(additionalInfo);

        if constexpr (IsContiguous<InputBuffer>) {
            reader_.position_ += value.size;
        } else {
            reader_.position_ = std::next(reader_.position_, value.size);
        }

        return status_code::success;
    }
    constexpr status_code decode(as_array_any &value, major_type major, byte additionalInfo) {
        if (major != major_type::Array) {
            // throw std::runtime_error("Invalid major type for array");
            return status_code::invalid_major_type_for_array;
        }
        value.size = decode_unsigned(additionalInfo);
        return status_code::success;
    }
    constexpr status_code decode(as_map_any &value, major_type major, byte additionalInfo) {
        if (major != major_type::Map) {
            // throw std::runtime_error("Invalid major type for map");
            return status_code::invalid_major_type_for_map;
        }
        value.size = decode_unsigned(additionalInfo);
        return status_code::success;
    }
    constexpr status_code decode(as_tag_any &value, major_type major, byte additionalInfo) {
        if (major != major_type::Tag) {
            // throw std::runtime_error("Invalid major type for tag");
            return status_code::invalid_major_type_for_tag;
        }
        value.tag = decode_unsigned(additionalInfo);
        return status_code::success;
    }

    template <typename T> constexpr status_code decode(T &value) {
        if (reader_.empty(data_)) {
            // throw std::runtime_error("Unexpected end of input");
            return status_code::incomplete;
        }

        const auto [majorType, additionalInfo] = read_initial_byte();

        // fmt::print("decoding {}, major: {}, additional info: {}\n", nameof::nameof_short_type<T>(), magic_enum::enum_name(majorType),
        //            additionalInfo);

        return decode(value, majorType, additionalInfo);
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
            return byte_range_view{result};
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

    template <typename Dec> struct status_collector {
        Dec                         &dec_;
        [[maybe_unused]] size_t      index{0};
        [[maybe_unused]] status_code result{status_code::success};

        template <typename U> constexpr bool operator()(U &&arg) {
            if constexpr (std::is_same_v<void, decltype(dec_.decode(arg))>) {
                dec_.decode(arg);
            } else {
                result = dec_.decode(arg);
                // fmt::print("status: {}, for index {}\n", status_message(result), index);
                index++; // TODO: Will be used later
                return result == status_code::success;
            }
            return false;
        }
    };

    template <typename T> constexpr auto decode_wrapped_group(T &&) {
        using tuple_type      = std::decay_t<T>;
        constexpr auto size_  = std::tuple_size_v<tuple_type>;
        auto           status = status_code::success;
        if constexpr (size_ > 1 && Options::wrap_groups) {
            status = this->decode(as_array{size_});
        }
        return status;
    }

    template <typename... Args> constexpr auto applier(Args &&...args) {
        status_collector<self_t> collect_status{*this};
        [[maybe_unused]] auto    success = (collect_status(args) && ...);
        return collect_status.result;
    }

    constexpr auto tell() const noexcept {
        if constexpr (IsContiguous<InputBuffer>) {
            return /* Iterator */ data_.begin() + reader_.position_;
        } else {
            return reader_.position_;
        }
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

    constexpr status_code decode(as_array value) {
        validate_size(major_type::Array, value.size_);
        return status_code::success;
    }
    template <typename... Ts> constexpr status_code decode(wrap_as_array<Ts...> value) {
        validate_size(major_type::Array, value.size_);
        return std::apply([this](auto &&...args) { return detail::underlying<T>(this).applier(std::forward<decltype(args)>(args)...); },
                          value.values_);
    }
    constexpr status_code decode(as_map value) {
        validate_size(major_type::Map, value.size_);
        return status_code::success;
    }
};

template <typename T> struct enum_decoder {

    template <IsEnum U> constexpr status_code decode(U &value, major_type major, std::byte additionalInfo) {
        using underlying_type = std::underlying_type_t<U>;
        if constexpr (IsSigned<underlying_type>) {
            if (major > major_type::NegativeInteger) {
                // throw std::runtime_error("Invalid major type for enum");
                return status_code::invalid_major_type_for_enum;
            }
        } else if constexpr (IsUnsigned<underlying_type>) {
            if (major != major_type::UnsignedInteger) {
                // throw std::runtime_error("Invalid major type for enum");
                return status_code::invalid_major_type_for_enum;
            }
        } else {
            // throw std::runtime_error("Invalid enum type");
            return status_code::invalid_major_type_for_enum;
        }

        underlying_type result;
        auto            status = detail::underlying<T>(this).decode(result, major, additionalInfo);
        value                  = static_cast<U>(result);
        return status;
    }

    template <IsEnum U> constexpr status_code decode(U &value) {
        auto [major, additionalInfo] = detail::underlying<T>(this).read_initial_byte();
        return decode(value, major, additionalInfo);
    }
};

template <typename InputBuffer> inline auto make_decoder(InputBuffer &buffer) {
    return decoder<InputBuffer, Options<default_expected, default_wrapping>, cbor_header_decoder, enum_decoder>(buffer);
}

} // namespace cbor::tags