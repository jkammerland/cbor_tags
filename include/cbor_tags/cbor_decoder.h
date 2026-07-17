#pragma once

#include "cbor_concepts.h"
#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_integer.h"
#include "cbor_tags/cbor_reflection.h"
#include "cbor_tags/cbor_simple.h"
#include "cbor_tags/detail/cbor_item.h"
#include "cbor_tags/detail/cbor_raw_view_decode.h"
#include "cbor_tags/detail/cbor_variant_dispatch.h"
#include "cbor_tags/float16_ieee754.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
// #include <fmt/base.h>
// #include <fmt/ranges.h>
#include <exception>
// #include <fmt/base.h>
#include <iterator>
#include <new>
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
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

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

template <HasReserve T> constexpr void reserve_for_append(T &value, std::uint64_t additional_size) {
    using size_type = typename T::size_type;

    const auto current_size = value.size();
    const auto maximum_size = [&]() {
        if constexpr (requires { value.max_size(); }) {
            return value.max_size();
        } else {
            return std::numeric_limits<size_type>::max();
        }
    }();

    if (std::cmp_greater(current_size, maximum_size) || std::cmp_greater(additional_size, maximum_size - current_size)) {
        throw std::length_error("CBOR string length exceeds target container max_size");
    }

    value.reserve(static_cast<size_type>(current_size + static_cast<size_type>(additional_size)));
}

template <typename InputBuffer, typename Reader>
constexpr status_code skip_sized_string_payload(Reader &reader, const InputBuffer &data, std::uint64_t length) {
    using size_type = typename Reader::size_type;

    if (length == 0) {
        return status_code::success;
    }

    if constexpr (std::numeric_limits<size_type>::max() < std::numeric_limits<std::uint64_t>::max()) {
        if (length > static_cast<std::uint64_t>(std::numeric_limits<size_type>::max())) {
            return status_code::error;
        }
    }

    const auto needed = static_cast<size_type>(length);
    if (reader.empty(data, needed - 1)) {
        return status_code::incomplete;
    }

    if constexpr (IsContiguous<InputBuffer>) {
        reader.position_ += needed;
    } else {
        if (std::cmp_greater(needed, std::numeric_limits<std::ptrdiff_t>::max())) {
            return status_code::error;
        }
        reader.position_ = std::next(reader.position_, static_cast<std::ptrdiff_t>(needed));
        reader.current_offset_ += needed;
    }

    return status_code::success;
}

} // namespace detail

template <typename InputBuffer, IsOptions Options, template <typename> typename... Decoders>
    requires CborInputBuffer<InputBuffer>
struct decoder : public Decoders<decoder<InputBuffer, Options, Decoders...>>... {
  private:
    struct decode_size_bounds {
        std::uint64_t min_size{};
        std::uint64_t max_size{std::numeric_limits<std::uint64_t>::max()};
    };

  public:
    using self_t = decoder<InputBuffer, Options, Decoders...>;
    using Decoders<self_t>::decode...;

    using reader_type       = detail::reader<InputBuffer>;
    using size_type         = typename reader_type::size_type;
    using buffer_byte_t     = std::ranges::range_value_t<InputBuffer>;
    using input_buffer_type = InputBuffer;
    using byte              = std::byte;

    using iterator_t             = std::ranges::iterator_t<const InputBuffer>;
    using subrange               = std::ranges::subrange<iterator_t>;
    using bstr_view_t            = bstr_view<subrange>;
    using tstr_view_t            = tstr_view<subrange>;
    using raw_encoded_item_view  = encoded_item_view_for<InputBuffer>;
    using raw_encoded_array_view = encoded_array_view_for<InputBuffer>;
    using raw_encoded_map_view   = encoded_map_view_for<InputBuffer>;

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
        } catch (const std::bad_alloc &) { return unexpected<status_code>(status_code::out_of_memory); } catch (const std::length_error &) {
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
            // Default native integer decode intentionally slices through the target type.
            // strict_integer_decode keeps this check enabled for users that need representability.
            if constexpr (detail::strict_integer_decode_option_v<Options>) {
                if (!detail::unsigned_value_fits<T>(decoded)) {
                    return status_code::no_match_for_int_on_buffer;
                }
            }
            value = static_cast<T>(decoded);
        } else if (major == major_type::NegativeInteger) {
            const auto decoded = decode_unsigned(additionalInfo);
            // Default native integer decode intentionally slices through the target type.
            // strict_integer_decode keeps this check enabled for users that need representability.
            if constexpr (detail::strict_integer_decode_option_v<Options>) {
                if (!detail::negative_argument_fits<T>(decoded)) {
                    return status_code::no_match_for_int_on_buffer;
                }
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
        // Default native integer decode intentionally slices through the target type.
        // strict_integer_decode keeps this check enabled for users that need representability.
        if constexpr (detail::strict_integer_decode_option_v<Options>) {
            if (!detail::unsigned_value_fits<T>(decoded)) {
                return status_code::no_match_for_uint_on_buffer;
            }
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

    template <IsBinaryString T, std::size_t Min, std::size_t Max>
    constexpr status_code decode(bounded_size<T, Min, Max> &value, major_type major, byte additionalInfo) {
        return decode_bounded_bstr(value.value(), major, additionalInfo, Min, Max);
    }

    template <IsTextString T, std::size_t Min, std::size_t Max>
    constexpr status_code decode(bounded_size<T, Min, Max> &value, major_type major, byte additionalInfo) {
        return decode_bounded_tstr(value.value(), major, additionalInfo, Min, Max);
    }

    template <IsArray T, std::size_t Min, std::size_t Max>
    constexpr status_code decode(bounded_size<T, Min, Max> &value, major_type major, byte additionalInfo) {
        return decode_bounded_array(value.value(), major, additionalInfo, Min, Max);
    }

    template <IsMap T, std::size_t Min, std::size_t Max>
    constexpr status_code decode(bounded_size<T, Min, Max> &value, major_type major, byte additionalInfo) {
        return decode_bounded_map(value.value(), major, additionalInfo, Min, Max);
    }

    template <IsBinaryString T> constexpr status_code decode(dynamic_bounded_size<T> &value, major_type major, byte additionalInfo) {
        return decode_bounded_bstr(value.value(), major, additionalInfo, value.min_size(), value.max_size());
    }

    template <IsTextString T> constexpr status_code decode(dynamic_bounded_size<T> &value, major_type major, byte additionalInfo) {
        return decode_bounded_tstr(value.value(), major, additionalInfo, value.min_size(), value.max_size());
    }

    template <IsArray T> constexpr status_code decode(dynamic_bounded_size<T> &value, major_type major, byte additionalInfo) {
        return decode_bounded_array(value.value(), major, additionalInfo, value.min_size(), value.max_size());
    }

    template <IsMap T> constexpr status_code decode(dynamic_bounded_size<T> &value, major_type major, byte additionalInfo) {
        return decode_bounded_map(value.value(), major, additionalInfo, value.min_size(), value.max_size());
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

        return decode_definite_bstr(t, decode_unsigned(additionalInfo));
    }

    template <IsBinaryString T> constexpr status_code decode_definite_bstr(T &t, std::uint64_t bstring_size) {
        if constexpr (IsFixedArray<T> || (IsConstBinaryView<T> && detail::is_static_extent_span_v<T>)) {
            if (bstring_size != static_cast<std::uint64_t>(t.size())) {
                debug::println("Fixed array size mismatch: {} != {}", bstring_size, t.size());
                return status_code::unexpected_group_size;
            }
        }

        // Decode to intermediate form
        auto bstring = decode_bstring_payload(bstring_size);

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
        } else if constexpr (IsConstView<T>) {
            t = T(std::ranges::data(bstring), std::ranges::size(bstring));
        } else if constexpr (IsFixedArray<T>) {
            std::ranges::copy(bstring, t.begin());
        } else {
            if constexpr (HasReserve<T>) {
                detail::reserve_for_append(t, bstring_size);
            }
            detail::appender<T> appender_;
            if constexpr (IsContiguous<decltype(bstring)>) {
                appender_(t, bstring);
            } else {
                for (auto byte_value : bstring) {
                    appender_(t, static_cast<typename T::value_type>(byte_value));
                }
            }
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

        return decode_definite_tstr(t, decode_unsigned(additionalInfo));
    }

    template <IsTextString T> constexpr status_code decode_definite_tstr(T &t, std::uint64_t text_size) {
        // Decode the complete payload before mutating an owning target.
        auto text = decode_text_payload(text_size);
        if constexpr (IsConstView<T>) {
            t = std::move(text);
        } else {
            if constexpr (HasReserve<T> && std::ranges::sized_range<decltype(text)>) {
                detail::reserve_for_append(t, std::ranges::size(text));
            }
            detail::appender<T> appender_;
            if constexpr (IsContiguous<decltype(text)>) {
                appender_(t, text);
            } else {
                for (auto character : text) {
                    appender_(t, static_cast<typename T::value_type>(character));
                }
            }
        }
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

        return decode_definite_range(value, decode_unsigned(additionalInfo));
    }

    template <IsRangeOfCborValues T> constexpr status_code decode_definite_range(T &value, std::uint64_t length) {
        if constexpr (IsFixedArray<T>) {
            if (length != static_cast<std::uint64_t>(value.size())) {
                return status_code::unexpected_group_size;
            }
        }
        if constexpr (HasReserve<T>) {
            if (std::cmp_greater(length, std::numeric_limits<typename T::size_type>::max())) {
                throw std::length_error("CBOR array length exceeds target container size_type");
            }
            value.reserve(static_cast<typename T::size_type>(length));
        }
        detail::appender<T> appender_;
        for (auto i = length; i > 0; --i) {
            if constexpr (IsMap<T>) {
                using key_type    = typename T::key_type;
                using mapped_type = typename T::mapped_type;
                using value_type  = std::pair<key_type, mapped_type>;
                if constexpr (detail::can_propagate_container_allocator_for<key_type, T>() ||
                              detail::can_propagate_container_allocator_for<mapped_type, T>()) {
                    auto key          = detail::make_decode_value_for<key_type>(value);
                    auto mapped_value = detail::make_decode_value_for<mapped_type>(value);
                    auto status       = decode(key);
                    status            = status == status_code::success ? decode(mapped_value) : status;
                    if (status != status_code::success) {
                        return status;
                    }
                    value_type result{std::move(key), std::move(mapped_value)};
                    appender_(value, std::move(result));
                } else {
                    value_type result;
                    auto &[key, mapped_value] = result;
                    auto status               = decode(key);
                    status                    = status == status_code::success ? decode(mapped_value) : status;
                    if (status != status_code::success) {
                        return status;
                    }
                    appender_(value, std::move(result));
                }
            } else {
                using value_type = typename T::value_type;
                auto result      = detail::make_decode_value_for<value_type>(value);
                auto status      = decode(result);
                if (status != status_code::success) {
                    return status;
                }
                appender_(value, std::move(result));
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
        return decode_definite_tstr(value, decode_unsigned(additionalInfo));
    }

    constexpr status_code decode_definite_tstr(std::string &value, std::uint64_t text_size) {
        auto text = decode_text_payload(text_size);
        if constexpr (std::ranges::sized_range<decltype(text)>) {
            detail::reserve_for_append(value, std::ranges::size(text));
        }
        detail::appender<std::string> appender_;
        if constexpr (IsContiguous<decltype(text)>) {
            appender_(value, text);
        } else {
            for (auto character : text) {
                appender_(value, static_cast<char>(character));
            }
        }
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
            return decode_definite_bstr(value, decode_unsigned(additionalInfo));
        }
    }

    constexpr status_code decode_definite_bstr(std::basic_string_view<std::byte> &value, std::uint64_t length_u64) {
        if constexpr (!IsContiguous<InputBuffer>) {
            return status_code::contiguous_view_on_non_contiguous_data;
        } else {
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
            return decode_definite_tstr(value, decode_unsigned(additionalInfo));
        }
    }

    constexpr status_code decode_definite_tstr(std::string_view &value, std::uint64_t text_size) {
        if constexpr (!IsContiguous<InputBuffer>) {
            return status_code::contiguous_view_on_non_contiguous_data;
        } else {
            value = decode_text_payload(text_size);
            return status_code::success;
        }
    }

    template <typename T> constexpr status_code decode(std::optional<T> &value, major_type major, byte additionalInfo) {
        if (major == major_type::Simple && additionalInfo == static_cast<byte>(22)) {
            value = std::nullopt;
            return status_code::success;
        } else {
            using value_type = std::remove_cvref_t<T>;
            auto t           = detail::make_decode_value_for_optional<value_type>(value);
            auto result      = decode(t, major, additionalInfo);
            if (result == status_code::success) {
                value = std::move(t);
            }
            return result;
        }
        return status_code::no_match_for_optional_on_buffer;
    }

    template <typename U> constexpr status_code decode([[maybe_unused]] as_named_map<U> value) {
#if CBOR_TAGS_HAS_NAMED_REFLECTION
        return detail::decode_named_map(*this, value.value_);
#else
        static_assert(always_false<std::remove_cvref_t<U>>::value,
                      "as_named_map requires named reflection (C++26 std::meta or Boost.PFR field names)");
        return status_code::error;
#endif
    }

    template <IsVariant Variant> constexpr status_code decode(Variant &value, major_type major, byte additionalInfo) {
        std::optional<std::uint64_t> tag;
        return decode_variant(value, major, additionalInfo, tag);
    }

    constexpr status_code decode(as_text_any &value, major_type major, byte additionalInfo) {
        if (major != major_type::TextString) {
            return status_code::no_match_for_tstr_on_buffer;
        }
        return decode_definite_tstr(value, decode_unsigned(additionalInfo));
    }
    constexpr status_code decode_definite_tstr(as_text_any &value, std::uint64_t text_size) {
        value.size = text_size;
        return detail::skip_sized_string_payload(reader_, data_, text_size);
    }
    constexpr status_code decode(as_bstr_any &value, major_type major, byte additionalInfo) {
        if (major != major_type::ByteString) {
            return status_code::no_match_for_bstr_on_buffer;
        }
        return decode_definite_bstr(value, decode_unsigned(additionalInfo));
    }
    constexpr status_code decode_definite_bstr(as_bstr_any &value, std::uint64_t bstring_size) {
        value.size = bstring_size;
        return detail::skip_sized_string_payload(reader_, data_, bstring_size);
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

    template <typename RawView>
        requires IsEncodedItemView<RawView>
    constexpr status_code decode_encoded_view(RawView &value, std::optional<major_type> expected_major, status_code major_mismatch) {
        if (reader_.empty(data_)) {
            return status_code::incomplete;
        }

        const auto                                             start = tell();
        detail::raw_encoded_item_bounds<iterator_t, size_type> bounds{};
        const auto                                             status =
            detail::read_raw_encoded_item_bounds<InputBuffer, size_type>(data_, start, expected_major, major_mismatch, bounds);
        if (status != status_code::success) {
            return status;
        }

        assign_encoded_view(value, bounds.start, bounds.cursor, bounds.size);
        if constexpr (IsContiguous<InputBuffer>) {
            reader_.position_ += bounds.size;
        } else {
            reader_.position_ = bounds.cursor;
            reader_.current_offset_ += bounds.size;
        }

        return status_code::success;
    }

    template <typename RawView>
        requires IsEncodedItemView<RawView>
    constexpr status_code decode(RawView &value) {
        if constexpr (IsEncodedArrayView<RawView>) {
            return decode_encoded_view(value, major_type::Array, status_code::no_match_for_array_on_buffer);
        } else if constexpr (IsEncodedMapView<RawView>) {
            return decode_encoded_view(value, major_type::Map, status_code::no_match_for_map_on_buffer);
        } else {
            return decode_encoded_view(value, std::nullopt, status_code::error);
        }
    }

    template <typename RawView>
        requires IsEncodedItemView<RawView>
    constexpr status_code decode(RawView &value, major_type major, byte) {
        if constexpr (IsEncodedArrayView<RawView>) {
            if (major != major_type::Array) {
                return status_code::no_match_for_array_on_buffer;
            }
        } else if constexpr (IsEncodedMapView<RawView>) {
            if (major != major_type::Map) {
                return status_code::no_match_for_map_on_buffer;
            }
        }
        reader_.seek(-1);
        return decode(value);
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

    template <IsBinaryString T>
    constexpr status_code decode_bounded_bstr(T &wrapped, major_type major, byte additionalInfo, std::size_t min, std::size_t max) {
        if (major != major_type::ByteString) {
            return status_code::no_match_for_bstr_on_buffer;
        }

        if constexpr (IsIndefiniteWrapper<T>) {
            if (additionalInfo != static_cast<byte>(31)) {
                return status_code::no_match_for_bstr_on_buffer;
            }
            return decode_indef_bstr<true>(wrapped.value_, {.min_size = min, .max_size = max});
        } else {
            if (additionalInfo == static_cast<byte>(31)) {
                if constexpr (IsConstView<T>) {
                    return status_code::no_match_for_bstr_on_buffer;
                } else if constexpr (IsFixedArray<T>) {
                    return status_code::unexpected_group_size;
                } else if constexpr (std::ranges::range<T>) {
                    return decode_indef_bstr<true>(wrapped, {.min_size = min, .max_size = max});
                } else {
                    return decode(wrapped, major, additionalInfo);
                }
            }

            const auto size   = decode_unsigned(additionalInfo);
            const auto status = detail::bounded_size_status(size, min, max);
            if (status != status_code::success) {
                return status;
            }
            return decode_definite_bstr(wrapped, size);
        }
    }

    template <IsTextString T>
    constexpr status_code decode_bounded_tstr(T &wrapped, major_type major, byte additionalInfo, std::size_t min, std::size_t max) {
        static_assert(!IsView<T> || IsConstView<T>, "if T is a view, it must be const, e.g tstr_view<std::deque<char>>");
        if constexpr (IsConstView<T> && (!IsContiguous<InputBuffer> && IsContiguous<T>)) {
            return status_code::contiguous_view_on_non_contiguous_data;
        }
        if (major != major_type::TextString) {
            return status_code::no_match_for_tstr_on_buffer;
        }

        if constexpr (IsIndefiniteWrapper<T>) {
            if (additionalInfo != static_cast<byte>(31)) {
                return status_code::no_match_for_tstr_on_buffer;
            }
            return decode_indef_tstr<true>(wrapped.value_, {.min_size = min, .max_size = max});
        } else {
            if (additionalInfo == static_cast<byte>(31)) {
                if constexpr (IsConstView<T>) {
                    return status_code::no_match_for_tstr_on_buffer;
                } else if constexpr (IsFixedArray<T>) {
                    return status_code::unexpected_group_size;
                } else if constexpr (std::ranges::range<T>) {
                    return decode_indef_tstr<true>(wrapped, {.min_size = min, .max_size = max});
                } else {
                    return decode(wrapped, major, additionalInfo);
                }
            }

            const auto size   = decode_unsigned(additionalInfo);
            const auto status = detail::bounded_size_status(size, min, max);
            if (status != status_code::success) {
                return status;
            }
            return decode_definite_tstr(wrapped, size);
        }
    }

    template <IsArray T>
    constexpr status_code decode_bounded_array(T &wrapped, major_type major, byte additionalInfo, std::size_t min, std::size_t max) {
        if (major != major_type::Array) {
            return status_code::no_match_for_array_on_buffer;
        }

        if constexpr (IsIndefiniteWrapper<T>) {
            if (additionalInfo != static_cast<byte>(31)) {
                return status_code::no_match_for_array_on_buffer;
            }
            return decode_indef_array<true>(wrapped.value_, {.min_size = min, .max_size = max});
        } else {
            if (additionalInfo == static_cast<byte>(31)) {
                if constexpr (IsFixedArray<T>) {
                    return status_code::unexpected_group_size;
                } else if constexpr (IsRangeOfCborValues<T>) {
                    return decode_indef_array<true>(wrapped, {.min_size = min, .max_size = max});
                } else {
                    return decode(wrapped, major, additionalInfo);
                }
            }

            const auto size   = decode_unsigned(additionalInfo);
            const auto status = detail::bounded_size_status(size, min, max);
            if (status != status_code::success) {
                return status;
            }
            if constexpr (IsArrayHeader<std::remove_cvref_t<T>>) {
                wrapped.size = size;
                return status_code::success;
            } else {
                return decode_definite_range(wrapped, size);
            }
        }
    }

    template <IsMap T>
    constexpr status_code decode_bounded_map(T &wrapped, major_type major, byte additionalInfo, std::size_t min, std::size_t max) {
        if (major != major_type::Map) {
            return status_code::no_match_for_map_on_buffer;
        }

        if constexpr (IsIndefiniteWrapper<T>) {
            if (additionalInfo != static_cast<byte>(31)) {
                return status_code::no_match_for_map_on_buffer;
            }
            return decode_indef_map<true>(wrapped.value_, {.min_size = min, .max_size = max});
        } else {
            if (additionalInfo == static_cast<byte>(31)) {
                if constexpr (IsRangeOfCborValues<T>) {
                    return decode_indef_map<true>(wrapped, {.min_size = min, .max_size = max});
                } else {
                    return decode(wrapped, major, additionalInfo);
                }
            }

            const auto size   = decode_unsigned(additionalInfo);
            const auto status = detail::bounded_size_status(size, min, max);
            if (status != status_code::success) {
                return status;
            }
            if constexpr (IsMapHeader<std::remove_cvref_t<T>>) {
                wrapped.size = size;
                return status_code::success;
            } else {
                return decode_definite_range(wrapped, size);
            }
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

    constexpr auto decode_bstring(byte additionalInfo) { return decode_bstring_payload(decode_unsigned(additionalInfo)); }

    constexpr auto decode_bstring_payload(std::uint64_t length) {
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

    constexpr auto decode_text(byte additionalInfo) { return decode_text_payload(decode_unsigned(additionalInfo)); }

    constexpr auto decode_text_payload(std::uint64_t length) {
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

    template <bool CheckBounds = false, typename T> constexpr status_code decode_indef_bstr(T &out, decode_size_bounds bounds = {}) {
        detail::appender<T>            appender_;
        [[maybe_unused]] std::uint64_t size{};
        while (true) {
            const auto                 start_position = reader_.position_;
            [[maybe_unused]] size_type start_offset{};
            if constexpr (!IsContiguous<InputBuffer>) {
                start_offset = reader_.current_offset_;
            }

            try {
                auto [major, additionalInfo] = read_initial_byte();
                if (major == major_type::Simple && additionalInfo == static_cast<byte>(31)) {
                    if constexpr (CheckBounds) {
                        return size >= bounds.min_size ? status_code::success : status_code::size_limit_exceeded;
                    } else {
                        return status_code::success;
                    }
                }
                if (major != major_type::ByteString || additionalInfo == static_cast<byte>(31)) {
                    return status_code::no_match_for_bstr_on_buffer;
                }

                const auto chunk_size = decode_unsigned(additionalInfo);
                if constexpr (CheckBounds) {
                    if (chunk_size > bounds.max_size || size > bounds.max_size - chunk_size) {
                        return status_code::size_limit_exceeded;
                    }
                }
                auto chunk = decode_bstring_payload(chunk_size);
                if constexpr (IsContiguous<decltype(chunk)>) {
                    appender_(out, chunk);
                } else {
                    for (auto b : chunk) {
                        appender_(out, static_cast<typename T::value_type>(b));
                    }
                }
                if constexpr (CheckBounds) {
                    size += chunk_size;
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

    template <bool CheckBounds = false, typename T> constexpr status_code decode_indef_tstr(T &out, decode_size_bounds bounds = {}) {
        detail::appender<T>            appender_;
        [[maybe_unused]] std::uint64_t size{};
        while (true) {
            const auto                 start_position = reader_.position_;
            [[maybe_unused]] size_type start_offset{};
            if constexpr (!IsContiguous<InputBuffer>) {
                start_offset = reader_.current_offset_;
            }

            try {
                auto [major, additionalInfo] = read_initial_byte();
                if (major == major_type::Simple && additionalInfo == static_cast<byte>(31)) {
                    if constexpr (CheckBounds) {
                        return size >= bounds.min_size ? status_code::success : status_code::size_limit_exceeded;
                    } else {
                        return status_code::success;
                    }
                }
                if (major != major_type::TextString || additionalInfo == static_cast<byte>(31)) {
                    return status_code::no_match_for_tstr_on_buffer;
                }

                const auto chunk_size = decode_unsigned(additionalInfo);
                if constexpr (CheckBounds) {
                    if (chunk_size > bounds.max_size || size > bounds.max_size - chunk_size) {
                        return status_code::size_limit_exceeded;
                    }
                }
                auto chunk = decode_text_payload(chunk_size);
                if constexpr (IsContiguous<decltype(chunk)>) {
                    appender_(out, chunk);
                } else {
                    for (auto c : chunk) {
                        appender_(out, static_cast<typename T::value_type>(c));
                    }
                }
                if constexpr (CheckBounds) {
                    size += chunk_size;
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

    template <bool CheckBounds = false, typename T> constexpr status_code decode_indef_array(T &value, decode_size_bounds bounds = {}) {
        detail::appender<T>            appender_;
        [[maybe_unused]] std::uint64_t size{};
        while (true) {
            const auto                 start_position = reader_.position_;
            [[maybe_unused]] size_type start_offset{};
            if constexpr (!IsContiguous<InputBuffer>) {
                start_offset = reader_.current_offset_;
            }

            try {
                auto [major, additionalInfo] = read_initial_byte();
                if (major == major_type::Simple && additionalInfo == static_cast<byte>(31)) {
                    if constexpr (CheckBounds) {
                        return size >= bounds.min_size ? status_code::success : status_code::size_limit_exceeded;
                    } else {
                        return status_code::success;
                    }
                }
                if constexpr (CheckBounds) {
                    if (size == bounds.max_size) {
                        return status_code::size_limit_exceeded;
                    }
                }

                using value_type = typename T::value_type;
                auto result      = detail::make_decode_value_for<value_type>(value);
                auto status      = decode(result, major, additionalInfo);
                if (status != status_code::success) {
                    return status;
                }
                appender_(value, std::move(result));
                if constexpr (CheckBounds) {
                    ++size;
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

    template <bool CheckBounds = false, typename T> constexpr status_code decode_indef_map(T &value, decode_size_bounds bounds = {}) {
        detail::appender<T>            appender_;
        [[maybe_unused]] std::uint64_t size{};
        while (true) {
            const auto                 start_position = reader_.position_;
            [[maybe_unused]] size_type start_offset{};
            if constexpr (!IsContiguous<InputBuffer>) {
                start_offset = reader_.current_offset_;
            }

            try {
                auto [major, additionalInfo] = read_initial_byte();
                if (major == major_type::Simple && additionalInfo == static_cast<byte>(31)) {
                    if constexpr (CheckBounds) {
                        return size >= bounds.min_size ? status_code::success : status_code::size_limit_exceeded;
                    } else {
                        return status_code::success;
                    }
                }
                if constexpr (CheckBounds) {
                    if (size == bounds.max_size) {
                        return status_code::size_limit_exceeded;
                    }
                }

                using key_type    = typename T::key_type;
                using mapped_type = typename T::mapped_type;
                using value_type  = std::pair<key_type, mapped_type>;
                if constexpr (detail::can_propagate_container_allocator_for<key_type, T>() ||
                              detail::can_propagate_container_allocator_for<mapped_type, T>()) {
                    auto key          = detail::make_decode_value_for<key_type>(value);
                    auto mapped_value = detail::make_decode_value_for<mapped_type>(value);
                    auto status       = decode(key, major, additionalInfo);
                    if (status == status_code::success) {
                        auto [mapped_major, mapped_additional_info] = read_initial_byte();
                        if (mapped_major == major_type::Simple && mapped_additional_info == static_cast<byte>(31)) {
                            return status_code::no_match_for_map_on_buffer;
                        }
                        status = decode(mapped_value, mapped_major, mapped_additional_info);
                    }
                    if (status != status_code::success) {
                        return status;
                    }
                    value_type result{std::move(key), std::move(mapped_value)};
                    appender_(value, std::move(result));
                    if constexpr (CheckBounds) {
                        ++size;
                    }
                } else {
                    value_type result{};
                    auto       status = decode(result.first, major, additionalInfo);
                    if (status == status_code::success) {
                        auto [mapped_major, mapped_additional_info] = read_initial_byte();
                        if (mapped_major == major_type::Simple && mapped_additional_info == static_cast<byte>(31)) {
                            return status_code::no_match_for_map_on_buffer;
                        }
                        status = decode(result.second, mapped_major, mapped_additional_info);
                    }
                    if (status != status_code::success) {
                        return status;
                    }
                    appender_(value, std::move(result));
                    if constexpr (CheckBounds) {
                        ++size;
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
        const auto info = std::to_integer<std::uint8_t>(additionalInfo);
        if (!detail::is_valid_cbor_argument_info(info)) {
            throw std::runtime_error("Invalid additional info for integer");
        }

        const auto payload_size = detail::cbor_argument_payload_size(info);
        if (payload_size > 0U && reader_.empty(data_, payload_size - 1U)) {
            throw parse_incomplete_exception("Unexpected end of input");
        }

        std::uint64_t value{};
        auto          status = status_code::success;
        const auto    ok     = detail::read_cbor_argument(info, value, status, [this](std::uint8_t &byte_value) {
            byte_value = static_cast<std::uint8_t>(reader_.read(data_));
            return true;
        });
        if (!ok) {
            throw std::runtime_error("Invalid additional info for integer");
        }
        return value;
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

    template <typename... Args> constexpr auto applier(Args &&...args) {
        status_collector<self_t> collect_status{*this};
        [[maybe_unused]] auto    success = (collect_status(std::forward<Args>(args)) && ...);
        return collect_status.result;
    }

    constexpr auto tell() const noexcept {
        if constexpr (IsContiguous<InputBuffer>) {
            return /* Iterator */ std::ranges::begin(data_) + static_cast<std::ptrdiff_t>(reader_.position_);
        } else {
            return /* Iterator */ reader_.position_; // TODO: actual Iterator
        }
    }

    // Internal reference, must be public, variadic friends only in c++26
    const InputBuffer &data_;
    reader_type        reader_;

  private:
    // Keep std::variant on the original pack-based fast path; generic trait-backed variant dispatch is slower here.
    template <typename... T>
    constexpr status_code decode_variant(std::variant<T...> &value, major_type major, byte additionalInfo,
                                         std::optional<std::uint64_t> &tag) {
        using namespace detail;
        static_assert(
            (!IsDynamicBoundedSizeWrapper<T> && ...),
            "dynamic_bounded_size cannot be decoded as a variant alternative because variant decoding creates a new unconfigured value");
        static_assert((IsCborMajor<T> && ...),
                      "All types must be CBOR major types, most likely you have a struct or class without a \"cbor_tag\" in the variant.");

        // TODO: Remove this requirement
        static_assert((std::is_default_constructible_v<T> && ...), "All types must be default constructible. Because in order to "
                                                                   "decode into the type, it must be default constructed first.");

        // Check ambiguous types in the variant.
        using Variant = std::variant<T...>;
        require_unambiguous_variant_dispatch<Variant>();
        // TODO: Revisit variant validity as a separate check from dispatch ambiguity.
        // Do not restore this as an unmatched-only guard; it misses invalid nested containers
        // and can drift from IsCborMajor/decoder overload truth.
        // static_assert(matching_major_types[MajorIndex::Unmatched] == 0, "Unmatched major types in variant");

        bool        saw_incomplete = false;
        status_code hard_error     = status_code::success;

        auto try_decode = [this, major, additionalInfo, &value, &tag, &saw_incomplete,
                           &hard_error]<bool CatchAllPass, typename U>() -> bool {
            using raw_type = std::remove_cvref_t<U>;
            if (hard_error != status_code::success) {
                return false;
            }
            if (!matches_major_dispatch<raw_type>(major)) {
                return false;
            }

            if (major == major_type::Simple && !detail::matches_simple_dispatch<CatchAllPass, raw_type>(additionalInfo)) {
                return false;
            }

            raw_type    decoded_value;
            status_code result;
            if constexpr (IsVariant<raw_type>) {
                result = this->decode_variant(decoded_value, major, additionalInfo, tag);
            } else if constexpr (IsTag<raw_type>) {
                if (!tag) {
                    tag = decode_unsigned(additionalInfo);
                }
                result = this->decode(decoded_value, *tag);
            } else {
                result = this->decode(decoded_value, major, additionalInfo);
            }

            if (result == status_code::success) {
                value = std::move(decoded_value);
                return true;
            } else if (result == status_code::incomplete) {
                saw_incomplete = true;
                return false;
            } else if (!detail::is_variant_alternative_mismatch(result)) {
                hard_error = result;
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
            if (hard_error != status_code::success) {
                return hard_error;
            }
            if (saw_incomplete) {
                return status_code::incomplete;
            }
            return status_code::no_match_in_variant_on_buffer;
        }
        return status_code::success;
    }

    template <IsVariant Variant>
    constexpr status_code decode_variant(Variant &value, major_type major, byte additionalInfo, std::optional<std::uint64_t> &tag) {
        using namespace detail;
        using variant_type = std::remove_cvref_t<Variant>;

        static_assert(
            detail::with_variant_alternatives<variant_type>([]<typename... Alternatives>() { return (IsCborMajor<Alternatives> && ...); }),
            "All variant alternatives must be CBOR major types, most likely you have a struct or class without a \"cbor_tag\" in the "
            "variant.");

        // TODO: Remove this requirement
        static_assert(
            detail::with_variant_alternatives<variant_type>(
                []<typename... Alternatives>() { return (std::is_default_constructible_v<Alternatives> && ...); }),
            "All variant alternatives must be default constructible. Because in order to decode into the type, each alternative must be "
            "default constructed first.");

        // Check ambiguous types in the variant.
        require_unambiguous_variant_dispatch<variant_type>();
        // TODO: Revisit variant validity as a separate check from dispatch ambiguity.
        // Do not restore this as an unmatched-only guard; it misses invalid nested containers
        // and can drift from IsCborMajor/decoder overload truth.
        // static_assert(matching_major_types[MajorIndex::Unmatched] == 0, "Unmatched major types in variant");

        bool        saw_incomplete = false;
        status_code hard_error     = status_code::success;

        auto try_decode = [this, major, additionalInfo, &value, &tag, &saw_incomplete,
                           &hard_error]<bool CatchAllPass, std::size_t I>() -> bool {
            using raw_type = detail::variant_alternative_t<I, variant_type>;
            if (hard_error != status_code::success) {
                return false;
            }
            if (!matches_major_dispatch<raw_type>(major)) {
                return false;
            }

            if (major == major_type::Simple && !detail::matches_simple_dispatch<CatchAllPass, raw_type>(additionalInfo)) {
                return false;
            }

            raw_type    decoded_value;
            status_code result;
            if constexpr (IsVariant<raw_type>) {
                result = this->decode_variant(decoded_value, major, additionalInfo, tag);
            } else if constexpr (IsTag<raw_type>) {
                if (!tag) {
                    tag = decode_unsigned(additionalInfo);
                }
                result = this->decode(decoded_value, *tag);
            } else {
                result = this->decode(decoded_value, major, additionalInfo);
            }

            if (result == status_code::success) {
                detail::variant_assign<I>(value, std::move(decoded_value));
                return true;
            } else if (result == status_code::incomplete) {
                saw_incomplete = true;
                return false;
            } else if (!detail::is_variant_alternative_mismatch(result)) {
                hard_error = result;
                return false;
            } else {
                return false;
            }
        };

        auto try_decode_alternatives = [&]<bool CatchAllPass, std::size_t... Is>(std::index_sequence<Is...>) {
            return (try_decode.template operator()<CatchAllPass, Is>() || ...);
        };

        bool found = false;
        if (major == major_type::Simple) {
            found = try_decode_alternatives.template operator()<false>(std::make_index_sequence<detail::variant_size_v<variant_type>>{});
            if (!found) {
                found = try_decode_alternatives.template operator()<true>(std::make_index_sequence<detail::variant_size_v<variant_type>>{});
            }
        } else {
            found = try_decode_alternatives.template operator()<false>(std::make_index_sequence<detail::variant_size_v<variant_type>>{});
        }
        if (!found) {
            if (hard_error != status_code::success) {
                return hard_error;
            }
            if (saw_incomplete) {
                return status_code::incomplete;
            }
            return status_code::no_match_in_variant_on_buffer;
        }
        return status_code::success;
    }

    template <typename RawView, typename Iterator>
    constexpr void assign_encoded_view(RawView &value, Iterator start, Iterator cursor, size_type size) const {
        using raw_view_type       = std::remove_cvref_t<RawView>;
        using raw_range_type      = typename raw_view_type::range_type;
        using raw_byte_view_type  = typename raw_view_type::byte_view_type;
        using raw_range_size_type = std::ranges::range_size_t<raw_range_type>;

        if constexpr (std::constructible_from<raw_range_type, Iterator, Iterator, raw_range_size_type>) {
            value = RawView{raw_byte_view_type{raw_range_type{start, cursor, static_cast<raw_range_size_type>(size)}}};
        } else if constexpr (IsContiguous<InputBuffer> && std::constructible_from<raw_range_type, std::span<const std::byte>>) {
            const auto *begin = std::ranges::data(data_) + reader_.position_;
            value             = RawView{raw_byte_view_type{
                raw_range_type{std::span<const std::byte>{reinterpret_cast<const std::byte *>(begin), static_cast<std::size_t>(size)}}}};
        } else {
            static_assert(always_false<raw_range_type>::value,
                          "Decode contiguous raw views only from contiguous buffers; use encoded_*_view_for<Buffer> or the decoder "
                          "raw_encoded_*_view aliases for non-contiguous buffers.");
        }
    }
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

template <CborInputBuffer InputBuffer> inline auto make_decoder(InputBuffer &buffer) {
    return decoder<InputBuffer, default_options, cbor_header_decoder, cbor_indefinite_decoder>(buffer);
}

template <template <typename> typename... Extensions, CborInputBuffer InputBuffer>
    requires(sizeof...(Extensions) > 0)
inline auto make_decoder(InputBuffer &buffer) {
    return decoder<InputBuffer, default_options, cbor_header_decoder, cbor_indefinite_decoder, Extensions...>(buffer);
}

template <IsOptions DecoderOptions, template <typename> typename... Extensions, CborInputBuffer InputBuffer>
inline auto make_decoder_with_options(InputBuffer &buffer) {
    return decoder<InputBuffer, DecoderOptions, cbor_header_decoder, cbor_indefinite_decoder, Extensions...>(buffer);
}

} // namespace cbor::tags
