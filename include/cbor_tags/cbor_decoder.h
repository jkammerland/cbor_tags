#pragma once

#include "cbor_concepts.h"
#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_concepts_checking.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_integer.h"
#include "cbor_tags/cbor_reflection.h"
#include "cbor_tags/cbor_simple.h"
#include "cbor_tags/float16_ieee754.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
// #include <fmt/base.h>
// #include <fmt/ranges.h>
#include <exception>
#include <functional>
// #include <fmt/base.h>
#include <iterator>
#include <memory>
// #include <magic_enum/magic_enum.hpp>
// #include <nameof.hpp>
#include "cbor_tags/cbor_tags_config.h"

#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cbor::tags {

namespace detail {
template <typename T> constexpr bool unsigned_value_fits(std::uint64_t value) {
    return value <= static_cast<std::uint64_t>(std::numeric_limits<T>::max());
}

template <typename T> constexpr bool negative_argument_fits(std::uint64_t value) {
    static_assert(std::is_signed_v<T>);
    constexpr auto min_value         = std::numeric_limits<T>::min();
    constexpr auto max_cbor_argument = static_cast<std::uint64_t>(-(min_value + T{1}));
    return value <= max_cbor_argument;
}

template <bool CatchAllPass, typename U> constexpr bool matches_simple_dispatch(std::byte additional_info) {
    using type = std::remove_cvref_t<U>;
    if constexpr (IsOptional<type>) {
        if (additional_info == static_cast<std::byte>(SimpleType::Null)) {
            return true;
        }
        return matches_simple_dispatch<CatchAllPass, typename type::value_type>(additional_info);
    } else if constexpr (IsVariant<type>) {
        return []<typename... Ts>(std::variant<Ts...> *, std::byte info) {
            return (matches_simple_dispatch<CatchAllPass, Ts>(info) || ...);
        }(static_cast<type *>(nullptr), additional_info);
    } else if constexpr (std::is_same_v<type, simple>) {
        const auto value = std::to_integer<std::uint8_t>(additional_info);
        return CatchAllPass && value <= static_cast<std::uint8_t>(SimpleType::Simple);
    } else if constexpr (IsSimple<type>) {
        return !CatchAllPass && compare_simple_value<type>(additional_info);
    } else {
        return false;
    }
}

} // namespace detail

template <typename InputBuffer, IsOptions Options, template <typename> typename... Decoders>
    requires ValidCborBuffer<InputBuffer>
struct decoder : public Decoders<decoder<InputBuffer, Options, Decoders...>>... {
    using self_t = decoder<InputBuffer, Options, Decoders...>;
    using Decoders<self_t>::decode...;

    using size_type         = std::size_t;
    using buffer_byte_t     = std::ranges::range_value_t<InputBuffer>;
    using input_buffer_type = InputBuffer;
    using byte              = std::byte;

    using iterator_t  = std::ranges::iterator_t<const InputBuffer>;
    using subrange    = std::ranges::subrange<iterator_t>;
    using bstr_view_t = bstr_view<subrange>;
    using tstr_view_t = tstr_view<subrange>;

    using expected_type   = typename Options::return_type;
    using unexpected_type = typename Options::error_type;
    using options         = Options;

    explicit decoder(const InputBuffer &data) : data_(data), reader_(data) {}

    template <typename... T> expected_type operator()(T &&...args) noexcept {
        try {
            status_collector<self_t> collect_status{*this};

            auto success = (collect_status(std::forward<T>(args)) && ...);

            if (!success) {
                return unexpected<decltype(collect_status.result)>(collect_status.result);
            }
            return expected_type{};
        } catch (const std::bad_alloc &) {
            return unexpected<status_code>(status_code::out_of_memory);
        } catch (const parse_incomplete_exception &) {
            return unexpected<status_code>(status_code::incomplete);
        } catch ([[maybe_unused]] const std::exception &e) {
            debug::println("Caught exception: {}", e.what());
            return unexpected<status_code>(status_code::error); // TODO: placeholder
        }
    }

    constexpr status_code decode(integer &value, major_type major, byte additionalInfo) {
        if (major == major_type::UnsignedInteger) {
            value = integer(decode_unsigned(additionalInfo));
        } else if (major == major_type::NegativeInteger) {
            const auto decoded = decode_unsigned(additionalInfo);
            value              = integer(negative(decoded + 1));
        } else {
            return status_code::no_match_for_int_on_buffer;
        }

        return status_code::success;
    }

    template <IsSigned T>
        requires(!std::is_same_v<T, integer>)
    constexpr status_code decode(T &value, major_type major, byte additionalInfo) {
        if (major == major_type::UnsignedInteger) {
            const auto decoded = decode_unsigned(additionalInfo);
            if (!detail::unsigned_value_fits<T>(decoded)) {
                return status_code::no_match_for_int_on_buffer;
            }
            value = static_cast<T>(decoded);
        } else if (major == major_type::NegativeInteger) {
            const auto decoded = decode_unsigned(additionalInfo);
            if (!detail::negative_argument_fits<T>(decoded)) {
                return status_code::no_match_for_int_on_buffer;
            }
            value = static_cast<T>(T{-1} - static_cast<T>(decoded));
        } else {
            return status_code::no_match_for_int_on_buffer;
        }

        return status_code::success;
    }

    template <IsEnum U> constexpr status_code decode(U &value, major_type major, std::byte additionalInfo) {
        using underlying_type = std::underlying_type_t<U>;
        underlying_type result;
        auto            status = this->decode(result, major, additionalInfo);
        if (status != status_code::success) {
            return status_code::no_match_for_enum_on_buffer;
        }
        value = static_cast<U>(result);
        return status_code::success;
    }

    template <IsEnum U> constexpr status_code decode(U &value) {
        auto [major, additionalInfo] = read_initial_byte();
        return decode(value, major, additionalInfo);
    }

    template <IsUnsigned T> constexpr status_code decode(T &value, major_type major, byte additionalInfo) {
        if (major != major_type::UnsignedInteger) {
            return status_code::no_match_for_uint_on_buffer;
        }
        const auto decoded = decode_unsigned(additionalInfo);
        if (!detail::unsigned_value_fits<T>(decoded)) {
            return status_code::no_match_for_uint_on_buffer;
        }
        value = static_cast<T>(decoded);

        return status_code::success;
    }

    constexpr status_code decode(negative &value, major_type major, byte additionalInfo) {
        if (major != major_type::NegativeInteger) {
            return status_code::no_match_for_nint_on_buffer;
        }
        const auto decoded = decode_unsigned(additionalInfo);
        value              = negative(decoded + 1);

        return status_code::success;
    }

    template <IsBinaryString T> constexpr status_code decode(T &t, major_type major, byte additionalInfo) {
        static_assert(!IsView<T> || IsConstView<T>, "if T is a view, it must be const, e.g std::span<const std::byte>");

        // Early validation
        if (major != major_type::ByteString) {
            return status_code::no_match_for_bstr_on_buffer;
        }

        if (additionalInfo == static_cast<byte>(31)) {
            if constexpr (IsConstView<T>) {
                return status_code::no_match_for_bstr_on_buffer;
            } else if constexpr (IsFixedArray<T>) {
                return status_code::unexpected_group_size;
            } else {
                return decode_indef_bstr(t);
            }
        }

        // Decode to intermediate form
        auto       bstring      = decode_bstring(additionalInfo);
        const auto bstring_size = static_cast<std::size_t>(std::ranges::distance(bstring.begin(), bstring.end()));

        // Now handle the target assignment based on contiguity constraints
        if constexpr (std::is_same_v<T, decltype(bstring)>) {
            // Direct assignment for same types
            t = std::move(bstring);
        } else if constexpr (IsConstView<T> && !IsContiguous<decltype(bstring)>) {
            // Can't directly construct a contiguous container from non-contiguous data
            // Either return an error or implement a copy-based approach

            // Error approach:
            return status_code::contiguous_view_on_non_contiguous_data;

            // Copy approach (if ownership semantics allow):
            // std::vector<typename T::value_type> temp(bstring.begin(), bstring.end());
            // t = T(...); // Construct from temp somehow
        } else if constexpr (IsFixedArray<T>) {
            // Fixed-size array assignment requires exact match
            if (bstring_size != t.size()) {
                debug::println("Fixed array size mismatch: {} != {}", bstring_size, t.size());
                return status_code::unexpected_group_size;
            }
            std::ranges::copy(bstring, t.begin());
        } else {
            // Standard case - construct from iterators
            t = T(bstring.begin(), bstring.end());
        }

        return status_code::success;
    }

    template <IsTextString T> constexpr status_code decode(T &t, major_type major, byte additionalInfo) {
        static_assert(!IsView<T> || IsConstView<T>, "if T is a view, it must be const, e.g tstr_view<std::deque<char>>");

        // Early return for incompatible view/buffer combination
        if constexpr (IsConstView<T> && (!IsContiguous<InputBuffer> && IsContiguous<T>)) {
            return status_code::contiguous_view_on_non_contiguous_data;
        }

        // Type check
        if (major != major_type::TextString) {
            return status_code::no_match_for_tstr_on_buffer;
        }

        if (additionalInfo == static_cast<byte>(31)) {
            if constexpr (IsConstView<T>) {
                return status_code::no_match_for_tstr_on_buffer;
            } else if constexpr (IsFixedArray<T>) {
                return status_code::unexpected_group_size;
            } else {
                return decode_indef_tstr(t);
            }
        }

        // Decode the text string
        t = decode_text(additionalInfo);

        return status_code::success;
    }

    template <IsRangeOfCborValues T> constexpr status_code decode(T &value, major_type major, byte additionalInfo) {
        if constexpr (IsMap<T>) {
            if (major != major_type::Map) {
                return status_code::no_match_for_map_on_buffer;
            }
        } else {
            if (major != major_type::Array) {
                return status_code::no_match_for_array_on_buffer;
            }
        }

        if (additionalInfo == static_cast<byte>(31)) {
            if constexpr (IsFixedArray<T>) {
                return status_code::unexpected_group_size;
            } else if constexpr (IsMap<T>) {
                return decode_indef_map(value);
            } else {
                return decode_indef_array(value);
            }
        }

        const auto length = decode_unsigned(additionalInfo);
        if constexpr (IsFixedArray<T>) {
            if (length != static_cast<std::uint64_t>(value.size())) {
                return status_code::unexpected_group_size;
            }
        }
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
            return status_code::no_match_for_tag_on_buffer;
        }
        if (decode_unsigned(additionalInfo) != N) {
            return status_code::no_match_for_tag;
        }
        return status_code::success;
    }

    template <std::uint64_t N> constexpr status_code decode(static_tag<N>, std::uint64_t tag) {
        if (tag != N) {
            return status_code::no_match_for_tag;
        }
        return status_code::success;
    }

    template <std::uint64_t N> constexpr status_code decode(static_tag<N> value) {
        auto [major, additionalInfo] = read_initial_byte();
        return decode(value, major, additionalInfo);
    }

    template <IsUnsigned T> constexpr status_code decode(dynamic_tag<T> &value, major_type major, byte additionalInfo) {
        if (major != major_type::Tag) {
            return status_code::no_match_for_tag_on_buffer;
        }

        auto decoded_value = dynamic_tag<T>{decode_unsigned(additionalInfo)};
        if (decoded_value.cbor_tag != value.cbor_tag) {
            return status_code::no_match_for_tag;
        }

        return status_code::success;
    }

    template <IsUnsigned T> constexpr status_code decode(dynamic_tag<T> &value) {
        auto [major, additionalInfo] = read_initial_byte();
        return decode(value, major, additionalInfo);
    }

    template <IsTaggedTuple T> constexpr status_code decode(T &t, major_type major, byte additionalInfo) {
        if (major != major_type::Tag) {
            return status_code::no_match_for_tag_on_buffer;
        }

        auto tag = decode_unsigned(additionalInfo);
        if (tag != std::get<0>(t)) {
            return status_code::no_match_for_tag;
        }

        auto array_header_status = this->decode_wrapped_group(detail::tuple_tail(t));
        if (array_header_status != status_code::success) {
            return array_header_status;
        }
        return std::apply([this](auto &&...args) { return this->applier(std::forward<decltype(args)>(args)...); }, detail::tuple_tail(t));
    }

    template <IsTaggedTuple T> constexpr status_code decode(T &t, std::uint64_t tag) {
        if (tag != std::get<0>(t)) {
            return status_code::no_match_for_tag;
        }

        auto array_header_status = this->decode_wrapped_group(detail::tuple_tail(t));
        if (array_header_status != status_code::success) {
            return array_header_status;
        }
        return std::apply([this](auto &&...args) { return this->applier(std::forward<decltype(args)>(args)...); }, detail::tuple_tail(t));
    }

    template <typename T>
        requires(IsAggregate<T> && !IsClassWithDecodingOverload<self_t, T>)
    constexpr status_code decode(T &value) {
        auto &&tuple = to_tuple(value);

        // Decode the tag
        auto result = status_code::success;
        if constexpr (HasInlineTag<T>) {
            result = decode(static_tag<T::cbor_tag>{});
        } else if constexpr (IsTag<T>) {
            result = decode(std::get<0>(tuple));
        }
        // Check if the tag was decoded successfully
        if (result != status_code::success) {
            return result;
        }

        // Decode the group header
        auto group_status = status_code::success;
        if constexpr (HasInlineTag<T> || !IsTag<T>) {
            group_status = this->decode_wrapped_group(tuple);
        } else {
            group_status = this->decode_wrapped_group(detail::tuple_tail(tuple));
        }
        // Check if the group was decoded successfully
        if (group_status != status_code::success) {
            return group_status;
        }

        // Decode the group and return the result
        if constexpr (HasInlineTag<T> || !IsTag<T>) {
            return std::apply([this](auto &&...args) { return this->applier(std::forward<decltype(args)>(args)...); }, tuple);
        } else {
            return std::apply([this](auto &&...args) { return this->applier(std::forward<decltype(args)>(args)...); },
                              detail::tuple_tail(tuple));
        }
    }

    template <typename T>
        requires(IsAggregate<T> && !IsClassWithDecodingOverload<self_t, T>)
    constexpr status_code decode(T &value, major_type major, byte additionalInfo) {
        if (major != major_type::Tag) {
            return status_code::no_match_for_tag_on_buffer;
        }

        auto &&tuple = to_tuple(value);
        auto   tag   = decode_unsigned(additionalInfo);
        return this->decode_tagged_aggregate(value, tag, tuple);
    }

    template <typename T>
        requires(IsAggregate<T> && !IsClassWithDecodingOverload<self_t, T>)
    constexpr status_code decode(T &value, std::uint64_t tag) {
        auto &&tuple = to_tuple(value);
        return this->decode_tagged_aggregate(value, tag, tuple);
    }

    template <typename T>
        requires IsClassWithTagOverload<T> && IsClassWithDecodingOverload<self_t, T>
    constexpr status_code decode(T &value, std::uint64_t tag) {
        std::uint64_t class_tag;
        if constexpr (HasTagMember<T>) {
            class_tag = Access::cbor_tag(value);
        } else if constexpr (HasTagNonConstructible<T>) {
            class_tag = cbor_tag<T>();
        } else if constexpr (HasTagFreeFunction<T>) {
            class_tag = cbor_tag(value);
        }

        if (static_cast<decltype(tag)>(class_tag) != tag) {
            return status_code::no_match_for_tag;
        }
        return this->decode_without_tag(value);
    }

    template <typename T> constexpr status_code decode_tagged_aggregate(T &, const std::uint64_t tag, auto &&tuple) {
        static_assert(IsTag<T>, "Only tagged objects end up here. Otherwise they should have called the array overload because of tuple "
                                "cast. Are you calling this directly? Please don't.");

        if constexpr (HasInlineTag<T>) {
            if (tag != T::cbor_tag) {
                return status_code::no_match_for_tag;
            }
            auto array_header_status = this->decode_wrapped_group(tuple);
            if (array_header_status != status_code::success) {
                return array_header_status;
            }
            return std::apply([this](auto &&...args) { return this->applier(std::forward<decltype(args)>(args)...); }, tuple);
        } else {
            if (tag != std::get<0>(tuple)) {
                return status_code::no_match_for_tag;
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
        auto group_status = this->decode_wrapped_group(value);
        if (group_status != status_code::success) {
            return group_status;
        }
        return std::apply([this](auto &&...args) { return this->applier(std::forward<decltype(args)>(args)...); }, value);
    }

    constexpr status_code decode(bool &value, major_type major, byte additionalInfo) {
        if (major != major_type::Simple) {
            return status_code::no_match_for_simple_on_buffer;
        }
        if (additionalInfo == static_cast<byte>(20)) {
            value = false;
        } else if (additionalInfo == static_cast<byte>(21)) {
            value = true;
        } else {

            return status_code::no_match_for_tag_simple_on_buffer;
        }
        return status_code::success;
    }

    constexpr status_code decode(std::nullptr_t &value, major_type major, byte additionalInfo) {
        if (major != major_type::Simple) {
            return status_code::no_match_for_simple_on_buffer;
        } else if (additionalInfo != static_cast<byte>(22)) {
            return status_code::no_match_for_tag_simple_on_buffer;
        }
        value = nullptr;
        return status_code::success;
    }

    constexpr status_code decode(float16_t &value, major_type major, byte additionalInfo) {
        if (major != major_type::Simple) {
            return status_code::no_match_for_simple_on_buffer;
        } else if (additionalInfo != static_cast<byte>(25)) {
            return status_code::no_match_for_tag_simple_on_buffer;
        }
        value = read_float16();
        return status_code::success;
    }

    constexpr status_code decode(float &value, major_type major, byte additionalInfo) {
        if (major != major_type::Simple) {
            return status_code::no_match_for_simple_on_buffer;
        } else if (additionalInfo != static_cast<byte>(26)) {
            return status_code::no_match_for_tag_simple_on_buffer;
        }
        value = read_float();
        return status_code::success;
    }

    constexpr status_code decode(double &value, major_type major, byte additionalInfo) {
        if (major != major_type::Simple) {
            return status_code::no_match_for_simple_on_buffer;
        } else if (additionalInfo != static_cast<byte>(27)) {
            return status_code::no_match_for_tag_simple_on_buffer;
        }
        value = read_double();
        return status_code::success;
    }

    constexpr status_code decode(std::string &value, major_type major, byte additionalInfo) {
        if (major != major_type::TextString) {
            return status_code::no_match_for_tstr_on_buffer;
        }
        if (additionalInfo == static_cast<byte>(31)) {
            return decode_indef_tstr(value);
        }
        auto text = decode_text(additionalInfo);
        value     = std::string(text);
        return status_code::success;
    }

    constexpr status_code decode(std::basic_string_view<std::byte> &value, major_type major, byte additionalInfo) {
        if constexpr (!IsContiguous<InputBuffer>) {
            return status_code::contiguous_view_on_non_contiguous_data;
        } else {
            if (major != major_type::ByteString) {
                return status_code::no_match_for_bstr_on_buffer;
            }
            if (additionalInfo == static_cast<byte>(31)) {
                return status_code::no_match_for_bstr_on_buffer;
            }
            const auto length_u64 = decode_unsigned(additionalInfo);
            if (length_u64 == 0) {
                value = {};
                return status_code::success;
            }

            if constexpr (std::numeric_limits<size_type>::max() < std::numeric_limits<std::uint64_t>::max()) {
                if (length_u64 > static_cast<std::uint64_t>(std::numeric_limits<size_type>::max())) {
                    return status_code::error;
                }
            }

            const auto length = static_cast<size_type>(length_u64);
            if (reader_.empty(data_, length - 1)) {
                return status_code::incomplete;
            }

            const auto *begin = std::ranges::data(data_) + reader_.position_;
            value             = std::basic_string_view<std::byte>(reinterpret_cast<const std::byte *>(begin), length);
            reader_.position_ += length;
            return status_code::success;
        }
    }

    constexpr status_code decode(std::string_view &value, major_type major, byte additionalInfo) {
        if constexpr (!IsContiguous<InputBuffer>) {
            return status_code::contiguous_view_on_non_contiguous_data;
        } else {
            if (major != major_type::TextString) {
                return status_code::no_match_for_tstr_on_buffer;
            }
            if (additionalInfo == static_cast<byte>(31)) {
                return status_code::no_match_for_tstr_on_buffer;
            }
            value = decode_text(additionalInfo);
            return status_code::success;
        }
    }

    template <typename T> constexpr status_code decode(std::optional<T> &value, major_type major, byte additionalInfo) {
        if (major == major_type::Simple && additionalInfo == static_cast<byte>(22)) {
            value = std::nullopt;
            return status_code::success;
        } else {
            using value_type = std::remove_cvref_t<T>;
            value_type t;
            auto       result = decode(t, major, additionalInfo);
            if (result == status_code::success) {
                value = std::move(t);
            }
            return result;
        }
        return status_code::no_match_for_optional_on_buffer;
    }

    template <typename U> constexpr status_code decode([[maybe_unused]] as_named_map<U> value) {
#if CBOR_TAGS_HAS_STD_REFLECTION
        return decode_named_map(value.value_);
#else
        static_assert(always_false<std::remove_cvref_t<U>>::value, "as_named_map requires C++26 static reflection");
        return status_code::error;
#endif
    }

    template <typename... T> constexpr status_code decode(std::variant<T...> &value, major_type major, byte additionalInfo) {
        using namespace detail;
        static_assert((IsCborMajor<T> && ...),
                      "All types must be CBOR major types, most likely you have a struct or class without a \"cbor_tag\" in the variant.");

        // TODO: Remove this requirement
        static_assert((std::is_default_constructible_v<T> && ...), "All types must be default constructible. Because in order to "
                                                                   "decode into the type, it must be default constructed first.");

        // Check ambiguous types in the variant.
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
        // TODO: Revisit variant validity as a separate check from dispatch ambiguity.
        // Do not restore this as an unmatched-only guard; it misses invalid nested containers
        // and can drift from IsCborMajor/decoder overload truth.
        // static_assert(matching_major_types[MajorIndex::Unmatched] == 0, "Unmatched major types in variant");
        static_assert(matching_major_types[MajorIndex::DynamicTag] == 0,
                      "Variant cannot contain dynamic tags, must be known at compile time, use as_tag_any to catch any tag");

        static_assert(no_ambigous_major_types_in_variant, "Variant has ambigous major types, if this would compile, only the first type \
                                                          (among the ambigous) would get decoded.");

        // Holder for the parsed tag value
        [[maybe_unused]] std::optional<std::uint64_t> tag;
        bool                                          saw_incomplete = false;

        auto try_decode = [this, major, additionalInfo, &value, &tag, &saw_incomplete]<bool CatchAllPass, typename U>() -> bool {
            if (!is_valid_major<major_type, U>(major)) {
                return false;
            }

            if (major == major_type::Simple && !detail::matches_simple_dispatch<CatchAllPass, U>(additionalInfo)) {
                return false;
            }

            U           decoded_value;
            status_code result;
            if constexpr (IsTag<U>) {
                if (!tag) {
                    tag = decode_unsigned(additionalInfo);
                }
                result = this->decode(decoded_value, *tag);
            } else /* Not tag */ {
                result = this->decode(decoded_value, major, additionalInfo);
            }

            if (result == status_code::success) {
                value = std::move(decoded_value);
                return true;
            } else if (result == status_code::incomplete) {
                saw_incomplete = true;
                return false;
            } else {
                return false;
            }
        };

        bool found = false;
        if (major == major_type::Simple) {
            found = (try_decode.template operator()<false, T>() || ...);
            if (!found) {
                found = (try_decode.template operator()<true, T>() || ...);
            }
        } else {
            found = (try_decode.template operator()<false, T>() || ...);
        }
        if (!found) {
            if (saw_incomplete) {
                return status_code::incomplete;
            }
            return status_code::no_match_in_variant_on_buffer;
        }
        return status_code::success;
    }

    constexpr status_code decode(as_text_any &value, major_type major, byte additionalInfo) {
        if (major != major_type::TextString) {
            return status_code::no_match_for_tstr_on_buffer;
        }
        value.size = decode_unsigned(additionalInfo);
        if (value.size == 0) {
            return status_code::success;
        }

        if constexpr (std::numeric_limits<size_type>::max() < std::numeric_limits<std::uint64_t>::max()) {
            if (value.size > static_cast<std::uint64_t>(std::numeric_limits<size_type>::max())) {
                return status_code::error;
            }
        }

        const auto needed = static_cast<size_type>(value.size);
        if (reader_.empty(data_, needed - 1)) {
            return status_code::incomplete;
        }

        if constexpr (IsContiguous<InputBuffer>) {
            reader_.position_ += needed;
        } else {
            if (needed > static_cast<size_type>(std::numeric_limits<std::ptrdiff_t>::max())) {
                return status_code::error;
            }
            reader_.position_ = std::next(reader_.position_, static_cast<std::ptrdiff_t>(needed));
            reader_.current_offset_ += needed;
        }

        return status_code::success;
    }
    constexpr status_code decode(as_bstr_any &value, major_type major, byte additionalInfo) {
        if (major != major_type::ByteString) {
            return status_code::no_match_for_bstr_on_buffer;
        }
        value.size = decode_unsigned(additionalInfo);
        if (value.size == 0) {
            return status_code::success;
        }

        if constexpr (std::numeric_limits<size_type>::max() < std::numeric_limits<std::uint64_t>::max()) {
            if (value.size > static_cast<std::uint64_t>(std::numeric_limits<size_type>::max())) {
                return status_code::error;
            }
        }

        const auto needed = static_cast<size_type>(value.size);
        if (reader_.empty(data_, needed - 1)) {
            return status_code::incomplete;
        }

        if constexpr (IsContiguous<InputBuffer>) {
            reader_.position_ += needed;
        } else {
            if (needed > static_cast<size_type>(std::numeric_limits<std::ptrdiff_t>::max())) {
                return status_code::error;
            }
            reader_.position_ = std::next(reader_.position_, static_cast<std::ptrdiff_t>(needed));
            reader_.current_offset_ += needed;
        }

        return status_code::success;
    }
    constexpr status_code decode(as_array_any &value, major_type major, byte additionalInfo) {
        if (major != major_type::Array) {
            return status_code::no_match_for_array_on_buffer;
        }
        value.size = decode_unsigned(additionalInfo);
        return status_code::success;
    }
    constexpr status_code decode(as_map_any &value, major_type major, byte additionalInfo) {
        if (major != major_type::Map) {
            return status_code::no_match_for_map_on_buffer;
        }
        value.size = decode_unsigned(additionalInfo);
        return status_code::success;
    }
    constexpr status_code decode(as_tag_any &value, major_type major, byte additionalInfo) {
        if (major != major_type::Tag) {
            return status_code::no_match_for_tag_on_buffer;
        }
        value.tag = decode_unsigned(additionalInfo);
        return status_code::success;
    }
    constexpr status_code decode(as_tag_any &value, std::uint64_t tag) {
        value.tag = tag;
        return status_code::success;
    }

    constexpr status_code decode(simple &value, major_type major, byte additionalInfo) {
        if (major != major_type::Simple) {
            return status_code::no_match_for_simple_on_buffer;
        } else if (additionalInfo < static_cast<byte>(24)) {
            value = simple{static_cast<simple::value_type>(additionalInfo)};
        } else if (additionalInfo == static_cast<byte>(24)) {
            value = simple{static_cast<simple::value_type>(read_uint8())};
        } else {
            return status_code::no_match_for_tag_simple_on_buffer;
        }

        return status_code::success;
    }

    template <typename C, bool DecodeTag = true>
        requires(IsClassWithDecodingOverload<self_t, C>)
    constexpr status_code decode_class_impl(C &value) {
        constexpr bool has_transcode      = HasTranscodeMethod<self_t, C>;
        constexpr bool has_decode         = HasDecodeMethod<self_t, C>;
        constexpr bool has_free_decode    = HasDecodeFreeFunction<self_t, C>;
        constexpr bool has_free_transcode = HasTranscodeFreeFunction<self_t, C>;

        static_assert(has_transcode ^ has_decode ^ has_free_decode ^ has_free_transcode,
                      "Class must have either (non-const) transcode(T& transcoder, O&& obj) or decode method, also do not forget to return "
                      "value from the "
                      "transcoding operation! "
                      "Give friend access if members are private, i.e friend cbor::tags::Access (full namespace is required)");

        // Automatic tag decoding - only performed if NOT in a variant context
        // In a variant context the tag is already decoded. The reason is that
        // a variant can hold multiple tags, and the tag is decoded once, then we find a matching type among the variant
        // alternatives. If we are here, then we have already checked that this is the right tag.
        if constexpr (DecodeTag && IsClassWithTagOverload<C>) {
            auto tag_status = this->decode(detail::get_major_6_tag_from_class(value));
            if (tag_status != status_code::success) {
                return tag_status;
            }
        }

        if constexpr (has_transcode) {
            auto result = Access::transcode(*this, value);
            return result ? status_code::success : result.error();
        } else if constexpr (has_decode) {
            auto result = Access::decode(*this, value);
            return result ? status_code::success : result.error();
        } else if constexpr (has_free_decode) {
            /* This requires an indirect call in order for some compilers to find the overload. */
            auto result = detail::adl_indirect_decode(*this, std::forward<C>(value));
            return result ? status_code::success : result.error();
        } else if (has_free_transcode) {
            /* Transcode does not require an indirect call, because no other methods exist with the same name (decode) */
            auto result = transcode(*this, std::forward<C>(value));
            return result ? status_code::success : result.error();
        }

        // throw std::runtime_error("This should never happen");
        return status_code::error;
    }

    template <typename C>
        requires(IsClassWithDecodingOverload<self_t, C>)
    constexpr status_code decode(C &value) {
        return decode_class_impl<C, true>(value);
    }

    template <typename C>
        requires(IsClassWithDecodingOverload<self_t, C>)
    constexpr status_code decode_without_tag(C &value) {
        return decode_class_impl<C, false>(value);
    }

    template <typename C>
        requires(IsClassWithDecodingOverload<self_t, C>)
    constexpr status_code decode(C &value, major_type, byte) {
        reader_.seek(-1);
        return this->decode(value);
    }

    template <typename T> constexpr status_code decode(T &value) {
        if (reader_.empty(data_)) {
            return status_code::incomplete;
        }
        const auto [majorType, additionalInfo] = read_initial_byte();
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
        const auto length      = decode_unsigned(additionalInfo);
        const auto span_length = require_bytes(length);

        if constexpr (IsContiguous<InputBuffer>) {
            auto *begin  = std::ranges::data(data_) + reader_.position_;
            auto  result = std::span<const byte>(reinterpret_cast<const byte *>(begin), span_length);
            reader_.position_ += span_length;
            return result;
        } else {
            auto start = reader_.position_;
            auto it    = start;
            for (size_type i = 0; i < span_length; ++i) {
                ++it;
            }
            auto result       = subrange(start, it);
            reader_.position_ = it;
            reader_.current_offset_ += span_length;
            return bstr_view_t{result};
        }
    }

    constexpr auto decode_text(byte additionalInfo) {
        const auto length      = decode_unsigned(additionalInfo);
        const auto span_length = require_bytes(length);

        if constexpr (IsContiguous<InputBuffer>) {
            auto *begin  = std::ranges::data(data_) + reader_.position_;
            auto  result = std::string_view(reinterpret_cast<const char *>(begin), span_length);
            reader_.position_ += span_length;
            return result;
        } else {
            auto start = reader_.position_;
            auto it    = start;
            for (size_type i = 0; i < span_length; ++i) {
                ++it;
            }
            auto result       = subrange(start, it);
            reader_.position_ = it;
            reader_.current_offset_ += span_length;
            return tstr_view_t{result};
        }
    }

    template <typename T> constexpr status_code decode_indef_bstr(T &out) {
        detail::appender<T> appender_;
        while (true) {
            const auto                 start_position = reader_.position_;
            [[maybe_unused]] size_type start_offset{};
            if constexpr (!IsContiguous<InputBuffer>) {
                start_offset = reader_.current_offset_;
            }

            try {
                auto [major, additionalInfo] = read_initial_byte();
                if (major == major_type::Simple && additionalInfo == static_cast<byte>(31)) {
                    return status_code::success;
                }
                if (major != major_type::ByteString || additionalInfo == static_cast<byte>(31)) {
                    return status_code::no_match_for_bstr_on_buffer;
                }

                auto chunk = decode_bstring(additionalInfo);
                if constexpr (IsContiguous<decltype(chunk)>) {
                    appender_(out, chunk);
                } else {
                    for (auto b : chunk) {
                        appender_(out, static_cast<typename T::value_type>(b));
                    }
                }
            } catch (const parse_incomplete_exception &) {
                reader_.position_ = start_position;
                if constexpr (!IsContiguous<InputBuffer>) {
                    reader_.current_offset_ = start_offset;
                }
                throw;
            }
        }
    }

    template <typename T> constexpr status_code decode_indef_tstr(T &out) {
        detail::appender<T> appender_;
        while (true) {
            const auto                 start_position = reader_.position_;
            [[maybe_unused]] size_type start_offset{};
            if constexpr (!IsContiguous<InputBuffer>) {
                start_offset = reader_.current_offset_;
            }

            try {
                auto [major, additionalInfo] = read_initial_byte();
                if (major == major_type::Simple && additionalInfo == static_cast<byte>(31)) {
                    return status_code::success;
                }
                if (major != major_type::TextString || additionalInfo == static_cast<byte>(31)) {
                    return status_code::no_match_for_tstr_on_buffer;
                }

                auto chunk = decode_text(additionalInfo);
                if constexpr (IsContiguous<decltype(chunk)>) {
                    appender_(out, chunk);
                } else {
                    for (auto c : chunk) {
                        appender_(out, static_cast<typename T::value_type>(c));
                    }
                }
            } catch (const parse_incomplete_exception &) {
                reader_.position_ = start_position;
                if constexpr (!IsContiguous<InputBuffer>) {
                    reader_.current_offset_ = start_offset;
                }
                throw;
            }
        }
    }

    template <typename T> constexpr status_code decode_indef_array(T &value) {
        detail::appender<T> appender_;
        while (true) {
            const auto                 start_position = reader_.position_;
            [[maybe_unused]] size_type start_offset{};
            if constexpr (!IsContiguous<InputBuffer>) {
                start_offset = reader_.current_offset_;
            }

            try {
                auto [major, additionalInfo] = read_initial_byte();
                if (major == major_type::Simple && additionalInfo == static_cast<byte>(31)) {
                    return status_code::success;
                }

                using value_type = typename T::value_type;
                value_type result{};
                auto       status = decode(result, major, additionalInfo);
                if (status != status_code::success) {
                    return status;
                }
                appender_(value, result);
            } catch (const parse_incomplete_exception &) {
                reader_.position_ = start_position;
                if constexpr (!IsContiguous<InputBuffer>) {
                    reader_.current_offset_ = start_offset;
                }
                throw;
            }
        }
    }

    template <typename T> constexpr status_code decode_indef_map(T &value) {
        detail::appender<T> appender_;
        while (true) {
            const auto                 start_position = reader_.position_;
            [[maybe_unused]] size_type start_offset{};
            if constexpr (!IsContiguous<InputBuffer>) {
                start_offset = reader_.current_offset_;
            }

            try {
                auto [major, additionalInfo] = read_initial_byte();
                if (major == major_type::Simple && additionalInfo == static_cast<byte>(31)) {
                    return status_code::success;
                }

                using value_type = std::pair<typename T::key_type, typename T::mapped_type>;
                value_type result{};
                auto       status = decode(result.first, major, additionalInfo);
                status            = (status == status_code::success) ? decode(result.second) : status;
                if (status != status_code::success) {
                    return status;
                }
                appender_(value, result);
            } catch (const parse_incomplete_exception &) {
                reader_.position_ = start_position;
                if constexpr (!IsContiguous<InputBuffer>) {
                    reader_.current_offset_ = start_offset;
                }
                throw;
            }
        }
    }

    constexpr size_type require_bytes(std::uint64_t length) {
        if constexpr (std::numeric_limits<size_type>::max() < std::numeric_limits<std::uint64_t>::max()) {
            if (length > static_cast<std::uint64_t>(std::numeric_limits<size_type>::max())) {
                throw std::runtime_error("CBOR item exceeds buffer limits");
            }
        }
        const auto needed = static_cast<size_type>(length);
        if (needed == 0) {
            return 0;
        }
        if (reader_.empty(data_, needed - 1)) {
            throw parse_incomplete_exception("Unexpected end of input");
        }
        return needed;
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
            throw parse_incomplete_exception("Unexpected end of input");
        }
        return static_cast<uint8_t>(reader_.read(data_));
    }

    constexpr uint16_t read_uint16() {
        if (reader_.empty(data_, 1)) {
            throw parse_incomplete_exception("Unexpected end of input");
        }
        auto byte0 = static_cast<uint16_t>(reader_.read(data_));
        auto byte1 = static_cast<uint16_t>(reader_.read(data_));
        return static_cast<uint16_t>(byte0 << 8 | byte1);
    }

    constexpr uint32_t read_uint32() {
        if (reader_.empty(data_, 3)) {
            throw parse_incomplete_exception("Unexpected end of input");
        }
        auto byte0 = static_cast<uint32_t>(reader_.read(data_));
        auto byte1 = static_cast<uint32_t>(reader_.read(data_));
        auto byte2 = static_cast<uint32_t>(reader_.read(data_));
        auto byte3 = static_cast<uint32_t>(reader_.read(data_));
        return (byte0 << 24) | (byte1 << 16) | (byte2 << 8) | byte3;
    }

    constexpr uint64_t read_uint64() {
        if (reader_.empty(data_, 7)) {
            throw parse_incomplete_exception("Unexpected end of input");
        }
        auto byte0 = static_cast<uint64_t>(reader_.read(data_));
        auto byte1 = static_cast<uint64_t>(reader_.read(data_));
        auto byte2 = static_cast<uint64_t>(reader_.read(data_));
        auto byte3 = static_cast<uint64_t>(reader_.read(data_));
        auto byte4 = static_cast<uint64_t>(reader_.read(data_));
        auto byte5 = static_cast<uint64_t>(reader_.read(data_));
        auto byte6 = static_cast<uint64_t>(reader_.read(data_));
        auto byte7 = static_cast<uint64_t>(reader_.read(data_));
        return (byte0 << 56) | (byte1 << 48) | (byte2 << 40) | (byte3 << 32) | (byte4 << 24) | (byte5 << 16) | (byte6 << 8) | (byte7);
    }

    constexpr simple read_simple() {
        if (reader_.empty(data_)) {
            throw parse_incomplete_exception("Unexpected end of input");
        }
        return simple{static_cast<simple::value_type>(reader_.read(data_))};
    }

    // CBOR Float16 decoding function
    constexpr float16_t read_float16() {
        if (reader_.empty(data_, 1)) {
            throw parse_incomplete_exception("Unexpected end of input");
        }
        auto byte0 = static_cast<uint16_t>(reader_.read(data_));
        auto byte1 = static_cast<uint16_t>(reader_.read(data_));
        return float16_t{static_cast<uint16_t>((byte0 << 8) | byte1)};
    }

    constexpr float read_float() {
        uint32_t bits = read_uint32();
        return std::bit_cast<float>(bits);
    }

    constexpr double read_double() {
        uint64_t bits = read_uint64();
        return std::bit_cast<double>(bits);
    }

    constexpr auto read_initial_byte() {
        if (reader_.empty(data_)) {
            throw parse_incomplete_exception("Unexpected end of input");
        }

        const auto initialByte    = reader_.read(data_);
        const auto majorType      = static_cast<major_type>(static_cast<byte>(initialByte) >> 5);
        const auto additionalInfo = initialByte & static_cast<byte>(0x1F);

        return std::make_pair(majorType, additionalInfo);
    }

    template <typename Dec> struct status_collector {
        Dec                         &dec_;
        [[maybe_unused]] size_t      index{0};
        [[maybe_unused]] status_code result{status_code::success};

        template <typename U> constexpr bool operator()(U &&arg) /* TODO: missing forward, or just by ref */ {
            if constexpr (std::is_same_v<void, decltype(dec_.decode(arg))>) {
                dec_.decode(arg);
            } else {
                result = dec_.decode(arg);
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

#if CBOR_TAGS_HAS_STD_REFLECTION
    static constexpr bool named_key_seen(const std::vector<std::string> &seen, std::string_view key) {
        return std::ranges::any_of(seen, [key](const std::string &candidate) { return candidate == key; });
    }

    template <typename Object> constexpr void reset_named_optionals_and_extensions(Object &object) {
        using value_type = std::remove_cvref_t<Object>;
        reset_named_optionals_and_extensions_impl(object, std::make_index_sequence<detail::aggregate_member_count<value_type>()>{});
    }

    template <typename Object, std::size_t... Is>
    constexpr void reset_named_optionals_and_extensions_impl(Object &object, std::index_sequence<Is...>) {
        (reset_named_member<Object, Is>(object), ...);
    }

    template <typename Object, std::size_t I> constexpr void reset_named_member(Object &object) {
        auto  tuple      = to_tuple(object);
        auto &field      = std::get<I>(tuple);
        using field_type = std::remove_cvref_t<decltype(field)>;
        if constexpr (IsNamedGroupWrapper<field_type>) {
            reset_named_optionals_and_extensions(field.value_);
        } else if constexpr (IsNamedExtensionWrapper<field_type>) {
            field.value_.clear();
        } else if constexpr (IsOptional<field_type>) {
            field.reset();
        }
    }

    template <typename Object> constexpr status_code decode_named_map(Object &object) {
        auto [major, additionalInfo] = read_initial_byte();
        if (major != major_type::Map) {
            return status_code::no_match_for_map_on_buffer;
        }

        reset_named_optionals_and_extensions(object);
        std::vector<std::string> seen;

        if (additionalInfo == static_cast<byte>(31)) {
            while (true) {
                auto [key_major, key_additionalInfo] = read_initial_byte();
                if (key_major == major_type::Simple && key_additionalInfo == static_cast<byte>(31)) {
                    return validate_required_named_members(object, seen) ? status_code::success : status_code::unexpected_group_size;
                }
                auto entry_status = decode_named_map_entry(object, key_major, key_additionalInfo, seen);
                if (entry_status != status_code::success) {
                    return entry_status;
                }
            }
        }

        const auto pair_count = decode_unsigned(additionalInfo);
        seen.reserve(static_cast<std::size_t>(pair_count));
        for (std::uint64_t index = 0; index < pair_count; ++index) {
            auto entry_status = decode_named_map_entry(object, seen);
            if (entry_status != status_code::success) {
                return entry_status;
            }
        }

        return validate_required_named_members(object, seen) ? status_code::success : status_code::unexpected_group_size;
    }

    template <typename Object> constexpr status_code decode_named_map_entry(Object &object, std::vector<std::string> &seen) {
        std::string key;
        auto        key_status = decode(key);
        if (key_status != status_code::success) {
            return key_status;
        }
        return decode_named_map_value(object, key, seen);
    }

    template <typename Object>
    constexpr status_code decode_named_map_entry(Object &object, major_type key_major, byte key_additionalInfo,
                                                 std::vector<std::string> &seen) {
        std::string key;
        auto        key_status = decode(key, key_major, key_additionalInfo);
        if (key_status != status_code::success) {
            return key_status;
        }
        return decode_named_map_value(object, key, seen);
    }

    template <typename Object>
    constexpr status_code decode_named_map_value(Object &object, std::string_view key, std::vector<std::string> &seen) {
        auto value_status = status_code::success;
        if (decode_named_member_by_key(object, key, seen, value_status)) {
            return value_status;
        }
        if (decode_named_extension_by_key(object, key, value_status)) {
            return value_status;
        }
        return status_code::unexpected_group_size;
    }

    template <typename Object>
    constexpr bool decode_named_member_by_key(Object &object, std::string_view key, std::vector<std::string> &seen, status_code &status) {
        using value_type = std::remove_cvref_t<Object>;
        return decode_named_member_by_key_impl(object, key, seen, status,
                                               std::make_index_sequence<detail::aggregate_member_count<value_type>()>{});
    }

    template <typename Object, std::size_t... Is>
    constexpr bool decode_named_member_by_key_impl(Object &object, std::string_view key, std::vector<std::string> &seen,
                                                   status_code &status, std::index_sequence<Is...>) {
        bool matched = false;
        ((matched = matched || decode_named_member<Object, Is>(object, key, seen, status)), ...);
        return matched;
    }

    template <typename Object, std::size_t I>
    constexpr bool decode_named_member(Object &object, std::string_view key, std::vector<std::string> &seen, status_code &status) {
        using value_type = std::remove_cvref_t<Object>;
        auto  tuple      = to_tuple(object);
        auto &field      = std::get<I>(tuple);
        using field_type = std::remove_cvref_t<decltype(field)>;

        if constexpr (IsNamedGroupWrapper<field_type>) {
            return decode_named_member_by_key(field.value_, key, seen, status);
        } else if constexpr (IsNamedExtensionWrapper<field_type>) {
            return false;
        } else {
            constexpr auto field_name = detail::aggregate_member_name<value_type, I>();
            if (key != std::string_view{field_name}) {
                return false;
            }
            if (named_key_seen(seen, key)) {
                status = status_code::unexpected_group_size;
                return true;
            }
            seen.emplace_back(key);
            status = decode_named_field_value(field);
            return true;
        }
    }

    template <typename Field> constexpr status_code decode_named_field_value(Field &field) {
        using field_type = std::remove_cvref_t<Field>;
        if constexpr (IsOptional<field_type>) {
            typename field_type::value_type value{};
            auto                            status = decode(value);
            if (status == status_code::success) {
                field = std::move(value);
            }
            return status;
        } else {
            return decode(field);
        }
    }

    template <typename Object> constexpr bool decode_named_extension_by_key(Object &object, std::string_view key, status_code &status) {
        using value_type = std::remove_cvref_t<Object>;
        return decode_named_extension_by_key_impl(object, key, status,
                                                  std::make_index_sequence<detail::aggregate_member_count<value_type>()>{});
    }

    template <typename Object, std::size_t... Is>
    constexpr bool decode_named_extension_by_key_impl(Object &object, std::string_view key, status_code &status,
                                                      std::index_sequence<Is...>) {
        bool matched = false;
        ((matched = matched || decode_named_extension_member<Object, Is>(object, key, status)), ...);
        return matched;
    }

    template <typename Object, std::size_t I>
    constexpr bool decode_named_extension_member(Object &object, std::string_view key, status_code &status) {
        auto  tuple      = to_tuple(object);
        auto &field      = std::get<I>(tuple);
        using field_type = std::remove_cvref_t<decltype(field)>;
        if constexpr (IsNamedGroupWrapper<field_type>) {
            return decode_named_extension_by_key(field.value_, key, status);
        } else if constexpr (IsNamedExtensionWrapper<field_type>) {
            using extension_type = named_extension_value_t<field_type>;
            static_assert(IsMap<extension_type> && IsTextString<typename extension_type::key_type>,
                          "as_named_extension requires a map with text-string keys");
            static_assert(std::constructible_from<typename extension_type::key_type, std::string_view>,
                          "as_named_extension key type must be constructible from std::string_view");
            using key_type    = typename extension_type::key_type;
            using mapped_type = typename extension_type::mapped_type;
            key_type extension_key{key};
            if (field.value_.find(extension_key) != field.value_.end()) {
                status = status_code::unexpected_group_size;
                return true;
            }
            mapped_type mapped_value{};
            status = decode(mapped_value);
            if (status == status_code::success) {
                field.value_.emplace(std::move(extension_key), std::move(mapped_value));
            }
            return true;
        } else {
            return false;
        }
    }

    template <typename Object> constexpr bool validate_required_named_members(const Object &object, const std::vector<std::string> &seen) {
        using value_type = std::remove_cvref_t<Object>;
        return validate_required_named_members_impl(object, seen, std::make_index_sequence<detail::aggregate_member_count<value_type>()>{});
    }

    template <typename Object, std::size_t... Is>
    constexpr bool validate_required_named_members_impl(const Object &object, const std::vector<std::string> &seen,
                                                        std::index_sequence<Is...>) {
        return (required_named_member_present<Object, Is>(object, seen) && ...);
    }

    template <typename Object, std::size_t I>
    constexpr bool required_named_member_present(const Object &object, const std::vector<std::string> &seen) {
        using value_type  = std::remove_cvref_t<Object>;
        const auto  tuple = to_tuple(object);
        const auto &field = std::get<I>(tuple);
        using field_type  = std::remove_cvref_t<decltype(field)>;
        if constexpr (IsNamedGroupWrapper<field_type>) {
            return validate_required_named_members(field.value_, seen);
        } else if constexpr (IsNamedExtensionWrapper<field_type> || IsOptional<field_type>) {
            return true;
        } else {
            return named_key_seen(seen, std::string_view{detail::aggregate_member_name<value_type, I>()});
        }
    }
#endif

    template <typename... Args> constexpr auto applier(Args &&...args) {
        status_collector<self_t> collect_status{*this};
        [[maybe_unused]] auto    success = (collect_status(std::forward<Args>(args)) && ...);
        return collect_status.result;
    }

    constexpr auto tell() const noexcept {
        if constexpr (IsContiguous<InputBuffer>) {
            return /* Iterator */ data_.begin() + reader_.position_;
        } else {
            return /* Iterator */ reader_.position_; // TODO: actual Iterator
        }
    }

    // Internal reference, must be public, variadic friends only in c++26
    const InputBuffer          &data_;
    detail::reader<InputBuffer> reader_;
};

template <typename T> struct cbor_indefinite_decoder {
    using byte = std::byte;

    template <typename U>
    constexpr status_code decode_indefinite_with_major(as_indefinite<U> value, major_type major, byte additionalInfo) {
        auto &dec = detail::underlying<T>(this);
        if constexpr (IsBinaryString<U> && !IsBinaryHeader<U> && !IsConstView<U>) {
            if (major != major_type::ByteString || additionalInfo != static_cast<byte>(31)) {
                return status_code::no_match_for_bstr_on_buffer;
            }
            if constexpr (IsFixedArray<U>) {
                return status_code::unexpected_group_size;
            } else {
                return dec.decode_indef_bstr(value.value_);
            }
        } else if constexpr (IsTextString<U> && !IsTextHeader<U> && !IsConstView<U>) {
            if (major != major_type::TextString || additionalInfo != static_cast<byte>(31)) {
                return status_code::no_match_for_tstr_on_buffer;
            }
            if constexpr (IsFixedArray<U>) {
                return status_code::unexpected_group_size;
            } else {
                return dec.decode_indef_tstr(value.value_);
            }
        } else if constexpr (IsMap<U> && !IsMapHeader<U>) {
            if (major != major_type::Map || additionalInfo != static_cast<byte>(31)) {
                return status_code::no_match_for_map_on_buffer;
            }
            return dec.decode_indef_map(value.value_);
        } else if constexpr (IsArray<U> && !IsArrayHeader<U>) {
            if (major != major_type::Array || additionalInfo != static_cast<byte>(31)) {
                return status_code::no_match_for_array_on_buffer;
            }
            if constexpr (IsFixedArray<U>) {
                return status_code::unexpected_group_size;
            } else {
                return dec.decode_indef_array(value.value_);
            }
        } else {
            return status_code::error;
        }
    }

    template <typename U> constexpr status_code decode(as_indefinite<U> value) {
        auto &dec = detail::underlying<T>(this);
        if (dec.reader_.empty(dec.data_)) {
            return status_code::incomplete;
        }

        const auto [major, additionalInfo] = dec.read_initial_byte();
        return decode_indefinite_with_major(value, major, additionalInfo);
    }

    template <typename U> constexpr status_code decode(as_indefinite<U> value, major_type major, byte additionalInfo) {
        return decode_indefinite_with_major(value, major, additionalInfo);
    }
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
        return (size == expectedSize) ? status_code::success : status_code::unexpected_group_size;
    }

    constexpr status_code                           decode(as_array value) { return validate_size(major_type::Array, value.size_); }
    template <typename... Ts> constexpr status_code decode(wrap_as_array<Ts...> value) {
        auto result = validate_size(major_type::Array, value.size_);
        if (result != status_code::success) {
            return result;
        }
        return std::apply([this](auto &&...args) { return detail::underlying<T>(this).applier(std::forward<decltype(args)>(args)...); },
                          value.values_);
    }
    constexpr status_code decode(as_map value) { return validate_size(major_type::Map, value.size_); }
};

template <typename InputBuffer> inline auto make_decoder(InputBuffer &buffer) {
    return decoder<InputBuffer, Options<default_expected, default_wrapping>, cbor_header_decoder, cbor_indefinite_decoder>(buffer);
}

namespace detail {

template <typename Byte> constexpr std::uint8_t scanner_byte_to_u8(Byte value) {
    if constexpr (std::same_as<std::remove_cvref_t<Byte>, std::byte>) {
        return std::to_integer<std::uint8_t>(value);
    } else {
        return static_cast<std::uint8_t>(value);
    }
}

struct tag_scan_frame {
    bool          indefinite{};
    major_type    major{};
    std::uint64_t remaining{};
    bool          map_expects_value{};
};

struct cbor_item_skipper {
    template <typename Iterator> static bool read_byte(Iterator &cursor, Iterator end, std::uint8_t &value, status_code &status) {
        if (cursor == end) {
            status = status_code::incomplete;
            return false;
        }
        value = scanner_byte_to_u8(*cursor);
        ++cursor;
        return true;
    }

    template <typename Iterator> static bool advance_bytes(Iterator &cursor, Iterator end, std::uint64_t length, status_code &status) {
        for (std::uint64_t index = 0; index < length; ++index) {
            if (cursor == end) {
                status = status_code::incomplete;
                return false;
            }
            ++cursor;
        }
        return true;
    }

    template <typename Iterator>
    static bool read_argument(Iterator &cursor, Iterator end, std::uint8_t additional_info, std::uint64_t &value, status_code &status) {
        if (additional_info < 24) {
            value = additional_info;
            return true;
        }
        if (additional_info > 27) {
            status = status_code::error;
            return false;
        }

        const auto byte_count = static_cast<std::uint8_t>(1U << (additional_info - 24U));
        value                 = 0;
        for (std::uint8_t index = 0; index < byte_count; ++index) {
            std::uint8_t byte{};
            if (!read_byte(cursor, end, byte, status)) {
                return false;
            }
            value = (value << 8U) | byte;
        }
        return true;
    }

    template <typename Iterator>
    static bool skip_indefinite_string(Iterator &cursor, Iterator end, major_type expected_major, status_code &status) {
        while (true) {
            if (cursor == end) {
                status = status_code::incomplete;
                return false;
            }
            if (scanner_byte_to_u8(*cursor) == 0xFFU) {
                ++cursor;
                return true;
            }

            std::uint8_t initial{};
            if (!read_byte(cursor, end, initial, status)) {
                return false;
            }
            const auto    chunk_major     = static_cast<major_type>(initial >> 5U);
            const auto    additional_info = static_cast<std::uint8_t>(initial & 0x1FU);
            std::uint64_t chunk_length{};
            if (chunk_major != expected_major || additional_info == 31U) {
                status = status_code::error;
                return false;
            }
            if (!read_argument(cursor, end, additional_info, chunk_length, status)) {
                return false;
            }
            if (!advance_bytes(cursor, end, chunk_length, status)) {
                return false;
            }
        }
    }

    template <typename Iterator> static bool skip_item(Iterator &cursor, Iterator end, status_code &status) {
        std::uint8_t initial{};
        if (!read_byte(cursor, end, initial, status)) {
            return false;
        }

        const auto major           = static_cast<major_type>(initial >> 5U);
        const auto additional_info = static_cast<std::uint8_t>(initial & 0x1FU);

        switch (major) {
        case major_type::UnsignedInteger:
        case major_type::NegativeInteger: {
            std::uint64_t ignored{};
            return additional_info != 31U && read_argument(cursor, end, additional_info, ignored, status);
        }
        case major_type::ByteString:
        case major_type::TextString: {
            if (additional_info == 31U) {
                return skip_indefinite_string(cursor, end, major, status);
            }
            std::uint64_t length{};
            return read_argument(cursor, end, additional_info, length, status) && advance_bytes(cursor, end, length, status);
        }
        case major_type::Array: {
            if (additional_info == 31U) {
                while (true) {
                    if (cursor == end) {
                        status = status_code::incomplete;
                        return false;
                    }
                    if (scanner_byte_to_u8(*cursor) == 0xFFU) {
                        ++cursor;
                        return true;
                    }
                    if (!skip_item(cursor, end, status)) {
                        return false;
                    }
                }
            }
            std::uint64_t length{};
            if (!read_argument(cursor, end, additional_info, length, status)) {
                return false;
            }
            for (std::uint64_t index = 0; index < length; ++index) {
                if (!skip_item(cursor, end, status)) {
                    return false;
                }
            }
            return true;
        }
        case major_type::Map: {
            if (additional_info == 31U) {
                while (true) {
                    if (cursor == end) {
                        status = status_code::incomplete;
                        return false;
                    }
                    if (scanner_byte_to_u8(*cursor) == 0xFFU) {
                        ++cursor;
                        return true;
                    }
                    if (!skip_item(cursor, end, status)) {
                        return false;
                    }
                    if (cursor == end) {
                        status = status_code::incomplete;
                        return false;
                    }
                    if (scanner_byte_to_u8(*cursor) == 0xFFU) {
                        status = status_code::error;
                        return false;
                    }
                    if (!skip_item(cursor, end, status)) {
                        return false;
                    }
                }
            }
            std::uint64_t length{};
            if (!read_argument(cursor, end, additional_info, length, status)) {
                return false;
            }
            if (length > (std::numeric_limits<std::uint64_t>::max() / 2U)) {
                status = status_code::error;
                return false;
            }
            for (std::uint64_t index = 0; index < length * 2U; ++index) {
                if (!skip_item(cursor, end, status)) {
                    return false;
                }
            }
            return true;
        }
        case major_type::Tag: {
            std::uint64_t ignored{};
            return additional_info != 31U && read_argument(cursor, end, additional_info, ignored, status) && skip_item(cursor, end, status);
        }
        case major_type::Simple:
            if (additional_info == 31U || additional_info == 28U || additional_info == 29U || additional_info == 30U) {
                status = status_code::error;
                return false;
            }
            if (additional_info < 24U) {
                return true;
            }
            return advance_bytes(cursor, end, std::uint64_t{1U << (additional_info - 24U)}, status);
        default: status = status_code::error; return false;
        }
    }
};

template <std::uint64_t... Tags> struct static_tag_filter {
    constexpr bool operator()(std::uint64_t tag) const { return ((tag == Tags) || ...); }
};

} // namespace detail

template <ValidCborBuffer Buffer> struct tag_payload_decoder {
    using buffer_type = std::remove_cvref_t<Buffer>;
    using iterator    = std::ranges::iterator_t<const buffer_type>;
    using subrange    = std::ranges::subrange<iterator>;

    subrange range_;

    template <typename... T> [[nodiscard]] auto operator()(T &&...values) {
        auto dec = cbor::tags::make_decoder(range_);
        return dec(std::forward<T>(values)...);
    }

    template <typename T> [[nodiscard]] auto decode(T &value) {
        auto dec = cbor::tags::make_decoder(range_);
        return dec.decode(value);
    }
};

template <ValidCborBuffer Buffer> struct tag_match {
    using buffer_type = std::remove_cvref_t<Buffer>;
    using iterator    = std::ranges::iterator_t<const buffer_type>;
    using subrange    = std::ranges::subrange<iterator>;

    const buffer_type *buffer_{};
    std::uint64_t      tag_{};
    iterator           payload_begin_{};
    iterator           payload_end_{};

    [[nodiscard]] constexpr std::uint64_t tag() const noexcept { return tag_; }
    [[nodiscard]] constexpr subrange      payload_range() const { return subrange{payload_begin_, payload_end_}; }

    [[nodiscard]] auto payload_span() const
        requires std::ranges::contiguous_range<const buffer_type>
    {
        const auto size = static_cast<std::size_t>(std::ranges::distance(payload_begin_, payload_end_));
        return std::span<const std::byte>{reinterpret_cast<const std::byte *>(std::to_address(payload_begin_)), size};
    }

    [[nodiscard]] auto make_decoder() const { return tag_payload_decoder<buffer_type>{payload_range()}; }

    template <typename T> [[nodiscard]] auto decode(T &value) const {
        auto range = payload_range();
        auto dec   = cbor::tags::make_decoder(range);
        return dec(value);
    }
};

template <ValidCborBuffer Buffer, typename Predicate> class tag_view : public std::ranges::view_interface<tag_view<Buffer, Predicate>> {
  public:
    using buffer_type = std::remove_cvref_t<Buffer>;
    using iterator_t  = std::ranges::iterator_t<const buffer_type>;
    using match_type  = tag_match<buffer_type>;

    class iterator {
      public:
        using value_type        = match_type;
        using difference_type   = std::ptrdiff_t;
        using iterator_category = std::input_iterator_tag;
        using iterator_concept  = std::input_iterator_tag;

        iterator() = default;
        explicit iterator(tag_view *view)
            : view_(view), cursor_(std::ranges::begin(*view->buffer_)), end_(std::ranges::end(*view->buffer_)) {
            find_next();
        }

        const match_type &operator*() const { return current_; }
        const match_type *operator->() const { return &current_; }

        iterator &operator++() {
            find_next();
            return *this;
        }

        void operator++(int) { ++(*this); }

        friend bool operator==(const iterator &lhs, std::default_sentinel_t) { return lhs.done_; }
        friend bool operator==(std::default_sentinel_t, const iterator &rhs) { return rhs.done_; }

      private:
        void fail(status_code status) {
            view_->status_ = status;
            done_          = true;
        }

        void pop_completed_frames() {
            while (!stack_.empty() && !stack_.back().indefinite && stack_.back().remaining == 0) {
                stack_.pop_back();
            }
        }

        bool consume_parent_item() {
            if (stack_.empty()) {
                return true;
            }
            auto &frame = stack_.back();
            if (frame.indefinite) {
                if (frame.major == major_type::Map) {
                    frame.map_expects_value = !frame.map_expects_value;
                }
                return true;
            }
            if (frame.remaining == 0) {
                return false;
            }
            --frame.remaining;
            return true;
        }

        bool consume_indefinite_break_if_present() {
            if (stack_.empty() || !stack_.back().indefinite || cursor_ == end_ || detail::scanner_byte_to_u8(*cursor_) != 0xFFU) {
                return false;
            }
            if (stack_.back().major == major_type::Map && stack_.back().map_expects_value) {
                fail(status_code::error);
                return true;
            }
            ++cursor_;
            stack_.pop_back();
            return true;
        }

        bool read_argument(std::uint8_t additional_info, std::uint64_t &value) {
            status_code status = status_code::success;
            if (!detail::cbor_item_skipper::read_argument(cursor_, end_, additional_info, value, status)) {
                fail(status);
                return false;
            }
            return true;
        }

        bool skip_indefinite_string(major_type major) {
            status_code status = status_code::success;
            if (!detail::cbor_item_skipper::skip_indefinite_string(cursor_, end_, major, status)) {
                fail(status);
                return false;
            }
            return true;
        }

        bool skip_bytes(std::uint64_t length) {
            status_code status = status_code::success;
            if (!detail::cbor_item_skipper::advance_bytes(cursor_, end_, length, status)) {
                fail(status);
                return false;
            }
            return true;
        }

        void find_next() {
            if (done_) {
                return;
            }

            while (true) {
                pop_completed_frames();

                if (cursor_ == end_) {
                    if (stack_.empty()) {
                        view_->status_ = status_code::success;
                        done_          = true;
                    } else {
                        fail(status_code::incomplete);
                    }
                    return;
                }

                if (consume_indefinite_break_if_present()) {
                    if (done_) {
                        return;
                    }
                    continue;
                }

                if (detail::scanner_byte_to_u8(*cursor_) == 0xFFU) {
                    fail(status_code::error);
                    return;
                }

                if (!consume_parent_item()) {
                    fail(status_code::error);
                    return;
                }

                std::uint8_t initial{};
                status_code  status = status_code::success;
                if (!detail::cbor_item_skipper::read_byte(cursor_, end_, initial, status)) {
                    fail(status);
                    return;
                }

                const auto major           = static_cast<major_type>(initial >> 5U);
                const auto additional_info = static_cast<std::uint8_t>(initial & 0x1FU);

                switch (major) {
                case major_type::UnsignedInteger:
                case major_type::NegativeInteger: {
                    std::uint64_t ignored{};
                    if (additional_info == 31U || !read_argument(additional_info, ignored)) {
                        if (!done_) {
                            fail(status_code::error);
                        }
                        return;
                    }
                    break;
                }
                case major_type::ByteString:
                case major_type::TextString: {
                    if (additional_info == 31U) {
                        if (!skip_indefinite_string(major)) {
                            return;
                        }
                    } else {
                        std::uint64_t length{};
                        if (!read_argument(additional_info, length) || !skip_bytes(length)) {
                            return;
                        }
                    }
                    break;
                }
                case major_type::Array: {
                    if (additional_info == 31U) {
                        stack_.push_back(detail::tag_scan_frame{.indefinite = true, .major = major_type::Array});
                    } else {
                        std::uint64_t length{};
                        if (!read_argument(additional_info, length)) {
                            return;
                        }
                        if (length > 0) {
                            stack_.push_back(detail::tag_scan_frame{.major = major_type::Array, .remaining = length});
                        }
                    }
                    break;
                }
                case major_type::Map: {
                    if (additional_info == 31U) {
                        stack_.push_back(detail::tag_scan_frame{.indefinite = true, .major = major_type::Map});
                    } else {
                        std::uint64_t length{};
                        if (!read_argument(additional_info, length)) {
                            return;
                        }
                        if (length > (std::numeric_limits<std::uint64_t>::max() / 2U)) {
                            fail(status_code::error);
                            return;
                        }
                        if (length > 0) {
                            stack_.push_back(detail::tag_scan_frame{.major = major_type::Map, .remaining = length * 2U});
                        }
                    }
                    break;
                }
                case major_type::Tag: {
                    std::uint64_t tag{};
                    if (additional_info == 31U || !read_argument(additional_info, tag)) {
                        if (!done_) {
                            fail(status_code::error);
                        }
                        return;
                    }
                    auto payload_begin = cursor_;
                    auto payload_end   = payload_begin;
                    status             = status_code::success;
                    if (!detail::cbor_item_skipper::skip_item(payload_end, end_, status)) {
                        fail(status);
                        return;
                    }

                    stack_.push_back(detail::tag_scan_frame{.major = major_type::Tag, .remaining = 1});
                    if (std::invoke(view_->predicate_, tag)) {
                        current_ = match_type{.buffer_        = view_->buffer_,
                                              .tag_           = tag,
                                              .payload_begin_ = payload_begin,
                                              .payload_end_   = payload_end};
                        return;
                    }
                    break;
                }
                case major_type::Simple:
                    if (additional_info == 31U || additional_info == 28U || additional_info == 29U || additional_info == 30U) {
                        fail(status_code::error);
                        return;
                    }
                    if (additional_info >= 24U && !skip_bytes(std::uint64_t{1U << (additional_info - 24U)})) {
                        return;
                    }
                    break;
                default: fail(status_code::error); return;
                }
            }
        }

        tag_view                           *view_{};
        iterator_t                          cursor_{};
        iterator_t                          end_{};
        std::vector<detail::tag_scan_frame> stack_{};
        match_type                          current_{};
        bool                                done_{};
    };

    constexpr tag_view(const buffer_type &buffer, Predicate predicate) : buffer_(&buffer), predicate_(std::move(predicate)) {}

    iterator begin() {
        status_ = status_code::success;
        return iterator{this};
    }
    std::default_sentinel_t end() const noexcept { return {}; }

    [[nodiscard]] status_code status() const noexcept { return status_; }
    [[nodiscard]] bool        failed() const noexcept { return status_ != status_code::success; }

  private:
    const buffer_type *buffer_;
    Predicate          predicate_;
    status_code        status_{status_code::success};

    friend class iterator;
};

template <std::uint64_t... Tags, ValidCborBuffer Buffer> [[nodiscard]] auto find_tags(const Buffer &buffer) {
    return tag_view<std::remove_cvref_t<Buffer>, detail::static_tag_filter<Tags...>>{buffer, {}};
}

template <ValidCborBuffer Buffer, typename Predicate> [[nodiscard]] auto find_tags(const Buffer &buffer, Predicate predicate) {
    return tag_view<std::remove_cvref_t<Buffer>, std::remove_cvref_t<Predicate>>{buffer, std::forward<Predicate>(predicate)};
}

} // namespace cbor::tags
