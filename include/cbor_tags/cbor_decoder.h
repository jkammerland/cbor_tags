#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_concepts_checking.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_integer.h"
#include "cbor_tags/cbor_reflection.h"
#include "cbor_tags/cbor_simple.h"
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
#include "cbor_tags/cbor_tags_config.h"

#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
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

    using size_type     = typename InputBuffer::size_type;
    using buffer_byte_t = typename InputBuffer::value_type;
    using byte          = std::byte;

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

            auto success = (collect_status(args) && ...);

            if (!success) {
                return unexpected<decltype(collect_status.result)>(collect_status.result);
            }
            return expected_type{};
        } catch (const std::bad_alloc &) {
            return unexpected<status_code>(status_code::out_of_memory);
        } catch ([[maybe_unused]] const std::exception &e) {
            debug::println("Caught exception: {}", e.what());
            return unexpected<status_code>(status_code::error); // TODO: placeholder
        }
    }

    template <IsSigned T> constexpr status_code decode(T &value, major_type major, byte additionalInfo) {
        if (major == major_type::UnsignedInteger) {
            value = decode_unsigned(additionalInfo);
        } else if (major == major_type::NegativeInteger) {
            value = decode_integer(additionalInfo);
        } else {
            return status_code::no_match_for_int_on_buffer;
        }

        return status_code::success;
    }

    template <IsEnum U> constexpr status_code decode(U &value, major_type major, std::byte additionalInfo) {
        using underlying_type = std::underlying_type_t<U>;
        if constexpr (IsSigned<underlying_type>) {
            if (major > major_type::NegativeInteger) {

                return status_code::no_match_for_enum_on_buffer;
            }
        } else if constexpr (IsUnsigned<underlying_type>) {
            if (major != major_type::UnsignedInteger) {
                return status_code::no_match_for_enum_on_buffer;
            }
        } else {

            return status_code::no_match_for_enum_on_buffer;
        }

        underlying_type result;
        auto            status = this->decode(result, major, additionalInfo);
        value                  = static_cast<U>(result);
        return status;
    }

    template <IsEnum U> constexpr status_code decode(U &value) {
        auto [major, additionalInfo] = read_initial_byte();
        return decode(value, major, additionalInfo);
    }

    template <IsUnsigned T> constexpr status_code decode(T &value, major_type major, byte additionalInfo) {
        if (major != major_type::UnsignedInteger) {
            return status_code::no_match_for_uint_on_buffer;
        }
        value = decode_unsigned(additionalInfo);

        return status_code::success;
    }

    constexpr status_code decode(negative &value, major_type major, byte additionalInfo) {
        if (major != major_type::NegativeInteger) {
            return status_code::no_match_for_nint_on_buffer;
        }
        value = negative(decode_unsigned(additionalInfo) + 1);

        return status_code::success;
    }

    template <IsBinaryString T> constexpr status_code decode(T &t, major_type major, byte additionalInfo) {
        static_assert(!IsView<T> || IsConstView<T>, "if T is a view, it must be const, e.g std::span<const std::byte>");

        // Early validation
        if (major != major_type::ByteString) {
            return status_code::no_match_for_bstr_on_buffer;
        }

        // Decode to intermediate form
        auto bstring = decode_bstring(additionalInfo);

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
            // Fixed-size array assignment
            if (bstring.size() > t.size()) {
                debug::println("Error: bstr size exceeds target array size, {} > {}", bstring.size(), t.size());
                return status_code::out_of_memory;
            }
            std::copy_n(bstring.begin(), t.size(), t.data());
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
        if (decoded_value.value != value.value) {
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
        requires(IsClassWithDecodingOverload<self_t, T>)
    constexpr status_code decode(T &value, std::uint64_t tag) {
        std::uint64_t class_tag;
        if constexpr (HasTagMember<T>) {
            class_tag = Access{}.cbor_tag(value);
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
            auto length = decode_unsigned(additionalInfo);
            if (reader_.empty(data_, length - 1)) {
                return status_code::incomplete;
            }
            value = std::basic_string_view<std::byte>(reinterpret_cast<const std::byte *>(&data_[reader_.position_]), length);
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

    template <typename... T> constexpr status_code decode(std::variant<T...> &value, major_type major, byte additionalInfo) {
        using namespace detail;
        static_assert((IsCborMajor<T> && ...),
                      "All types must be CBOR major types, most likely you have a struct or class without a \"cbor_tag\" in the variant.");
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
        // static_assert(matching_major_types[MajorIndex::Unmatched] == 0, "Unmatched major types in variant");
        static_assert(matching_major_types[MajorIndex::DynamicTag] == 0,
                      "Variant cannot contain dynamic tags, must be known at compile time, use as_tag_any to catch any tag");

        static_assert(no_ambigous_major_types_in_variant, "Variant has ambigous major types, if this would compile, only the first type \
                                                          (among the ambigous) would get decoded.");

        // Holder for the parsed tag value
        [[maybe_unused]] std::optional<std::uint64_t> tag;

        auto try_decode = [this, major, additionalInfo, &value, &tag]<typename U>() -> bool {
            if (!is_valid_major<major_type, U>(major)) {
                return false;
            }

            if constexpr (IsSimple<U>) {
                if (!compare_simple_value<U>(additionalInfo)) {
                    return false;
                }
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
            } else {
                return false;
            }
        };

        bool found = (try_decode.template operator()<T>() || ...);
        if (!found) {
            return status_code::no_match_in_variant_on_buffer;
        }
        return status_code::success;
    }

    constexpr status_code decode(as_text_any &value, major_type major, byte additionalInfo) {
        if (major != major_type::TextString) {
            return status_code::no_match_for_tstr_on_buffer;
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
            return status_code::no_match_for_bstr_on_buffer;
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
            this->decode(detail::get_major_6_tag_from_class(value));
        }

        if constexpr (has_transcode) {
            auto result = Access{}.transcode(*this, value);
            return result ? status_code::success : result.error();
        } else if constexpr (has_decode) {
            auto result = Access{}.decode(*this, value);
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
            return bstr_view_t{.range = result};
        }
    }

    constexpr auto decode_text(byte additionalInfo) {
        auto length = decode_unsigned(additionalInfo);
        if (reader_.empty(data_, length - 1)) {
            throw std::runtime_error("Unexpected end of input");
        }

        if constexpr (IsContiguous<InputBuffer>) {
            auto result =
                length > 0 ? std::string_view(reinterpret_cast<const char *>(&data_[reader_.position_]), length) : std::string_view{};
            reader_.position_ += length;
            return result;
        } else {
            auto it           = std::next(reader_.position_, length);
            auto result       = subrange(reader_.position_, it);
            reader_.position_ = it;
            return tstr_view_t{.range = result};
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
        auto byte0 = static_cast<uint16_t>(reader_.read(data_));
        auto byte1 = static_cast<uint16_t>(reader_.read(data_));
        return static_cast<uint16_t>(byte0 << 8 | byte1);
    }

    constexpr uint32_t read_uint32() {
        if (reader_.empty(data_, 3)) {
            throw std::runtime_error("Unexpected end of input");
        }
        auto byte0 = static_cast<uint32_t>(reader_.read(data_));
        auto byte1 = static_cast<uint32_t>(reader_.read(data_));
        auto byte2 = static_cast<uint32_t>(reader_.read(data_));
        auto byte3 = static_cast<uint32_t>(reader_.read(data_));
        return (byte0 << 24) | (byte1 << 16) | (byte2 << 8) | byte3;
    }

    constexpr uint64_t read_uint64() {
        if (reader_.empty(data_, 7)) {
            throw std::runtime_error("Unexpected end of input");
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
            throw std::runtime_error("Unexpected end of input");
        }
        return simple{static_cast<simple::value_type>(reader_.read(data_))};
    }

    // CBOR Float16 decoding function
    constexpr float16_t read_float16() {
        if (reader_.empty(data_, 1)) {
            throw std::runtime_error("Unexpected end of input");
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

    inline constexpr auto read_initial_byte() {
        if (reader_.empty(data_)) {
            throw std::runtime_error("Unexpected end of input");
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

        template <typename U> constexpr bool operator()(U &&arg) {
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

    template <typename... Args> constexpr auto applier(Args &&...args) {
        status_collector<self_t> collect_status{*this};
        [[maybe_unused]] auto    success = (collect_status(args) && ...);
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
    return decoder<InputBuffer, Options<default_expected, default_wrapping>, cbor_header_decoder>(buffer);
}

} // namespace cbor::tags