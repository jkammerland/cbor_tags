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

template <typename T> struct cbor_indefinite_decoder;

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

template <typename T> struct is_optional_tag : std::false_type {};

template <typename T> struct is_optional_tag<std::optional<T>> : std::bool_constant<IsTag<std::remove_cvref_t<T>>> {};

template <typename T> inline constexpr bool is_optional_tag_v = is_optional_tag<std::remove_cvref_t<T>>::value;

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

  private:
    template <typename> friend struct cbor_indefinite_decoder;

    class structural_scope {
      public:
        constexpr structural_scope() noexcept = default;
        constexpr explicit structural_scope(self_t &decoder) noexcept : decoder_(&decoder) {}
        structural_scope(const structural_scope &) = delete;
        constexpr structural_scope(structural_scope &&other) noexcept : decoder_(std::exchange(other.decoder_, nullptr)) {}

        constexpr structural_scope &operator=(const structural_scope &) = delete;
        constexpr structural_scope &operator=(structural_scope &&other) noexcept {
            if (this != &other) {
                release();
                decoder_ = std::exchange(other.decoder_, nullptr);
            }
            return *this;
        }

        constexpr ~structural_scope() { release(); }

      private:
        constexpr void release() noexcept {
            if (decoder_ != nullptr) {
                --decoder_->structural_depth_;
                decoder_ = nullptr;
            }
        }

        self_t *decoder_{};
    };

  public:
    struct entered_array {
        std::uint64_t size{};

        entered_array(const entered_array &)                          = delete;
        constexpr entered_array(entered_array &&) noexcept            = default;
        constexpr entered_array &operator=(const entered_array &)     = delete;
        constexpr entered_array &operator=(entered_array &&) noexcept = default;

      private:
        friend self_t;
        constexpr entered_array(std::uint64_t size_, structural_scope scope_) noexcept : size(size_), scope(std::move(scope_)) {}

        structural_scope scope;
    };

    struct entered_map {
        std::uint64_t size{};

        entered_map(const entered_map &)                          = delete;
        constexpr entered_map(entered_map &&) noexcept            = default;
        constexpr entered_map &operator=(const entered_map &)     = delete;
        constexpr entered_map &operator=(entered_map &&) noexcept = default;

      private:
        friend self_t;
        constexpr entered_map(std::uint64_t size_, structural_scope scope_) noexcept : size(size_), scope(std::move(scope_)) {}

        structural_scope scope;
    };

    struct entered_tag {
        std::uint64_t tag{};

        entered_tag(const entered_tag &)                          = delete;
        constexpr entered_tag(entered_tag &&) noexcept            = default;
        constexpr entered_tag &operator=(const entered_tag &)     = delete;
        constexpr entered_tag &operator=(entered_tag &&) noexcept = default;

      private:
        friend self_t;
        constexpr entered_tag(std::uint64_t tag_, structural_scope scope_) noexcept : tag(tag_), scope(std::move(scope_)) {}

        structural_scope scope;
    };

    [[nodiscard]] constexpr expected<entered_array, status_code> enter_array() {
        return enter_definite_container<entered_array>(major_type::Array, status_code::no_match_for_array_on_buffer);
    }

    [[nodiscard]] constexpr expected<entered_array, status_code> enter_array(std::uint64_t expected_size) {
        auto result = enter_array();
        if (!result) {
            return unexpected<status_code>(result.error());
        }
        if (result->size != expected_size) {
            return unexpected<status_code>(status_code::unexpected_group_size);
        }
        return result;
    }

    [[nodiscard]] constexpr expected<entered_map, status_code> enter_map() {
        return enter_definite_container<entered_map>(major_type::Map, status_code::no_match_for_map_on_buffer);
    }

    [[nodiscard]] constexpr expected<entered_map, status_code> enter_map(std::uint64_t expected_size) {
        auto result = enter_map();
        if (!result) {
            return unexpected<status_code>(result.error());
        }
        if (result->size != expected_size) {
            return unexpected<status_code>(status_code::unexpected_group_size);
        }
        return result;
    }

    [[nodiscard]] constexpr expected<entered_tag, status_code> enter_tag() {
        if (reader_.empty(data_)) {
            return unexpected<status_code>(status_code::incomplete);
        }

        const auto [major, additional_info] = read_initial_byte();
        if (major != major_type::Tag) {
            return unexpected<status_code>(status_code::no_match_for_tag_on_buffer);
        }
        if (additional_info == static_cast<byte>(31)) {
            return unexpected<status_code>(status_code::no_match_for_tag);
        }

        const auto tag   = decode_unsigned(additional_info);
        auto       scope = enter_structural_item();
        if (!scope) {
            return unexpected<status_code>(scope.error());
        }
        return entered_tag{tag, std::move(*scope)};
    }

    [[nodiscard]] constexpr expected<entered_tag, status_code> enter_tag(std::uint64_t expected_tag) {
        auto result = enter_tag();
        if (!result) {
            return unexpected<status_code>(result.error());
        }
        if (result->tag != expected_tag) {
            return unexpected<status_code>(status_code::no_match_for_tag);
        }
        return result;
    }

    template <typename... T> expected_type operator()(T &&...args) noexcept {
        try {
            auto result  = status_code::success;
            auto success = (decode_root_argument(std::forward<T>(args), result) && ...);

            if (!success) {
                return unexpected<status_code>(result);
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

        const auto bstring_size = decode_unsigned(additionalInfo);
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
            auto decoded = detail::make_decode_value_for<T>(t);
            if constexpr (HasReserve<T>) {
                if (std::cmp_greater(bstring_size, std::numeric_limits<typename T::size_type>::max())) {
                    throw std::length_error("CBOR byte string length exceeds target container size_type");
                }
                decoded.reserve(static_cast<typename T::size_type>(bstring_size));
            }
            detail::appender<T> appender_;
            if constexpr (IsContiguous<decltype(bstring)>) {
                appender_(decoded, bstring);
            } else {
                for (auto byte_value : bstring) {
                    appender_(decoded, static_cast<typename T::value_type>(byte_value));
                }
            }
            t = std::move(decoded);
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

        auto depth_scope = enter_structural_item();
        if (!depth_scope) {
            return depth_scope.error();
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

        auto tag_scope = enter_structural_item();
        if (!tag_scope) {
            return tag_scope.error();
        }

        auto tail = detail::tuple_tail(t);
        return this->decode_wrapped_group(tail, [this, &tail] {
            return std::apply([this](auto &&...args) { return this->applier(std::forward<decltype(args)>(args)...); }, tail);
        });
    }

    template <IsTaggedTuple T> constexpr status_code decode(T &t, std::uint64_t tag) {
        if (tag != std::get<0>(t)) {
            return status_code::no_match_for_tag;
        }

        auto tail = detail::tuple_tail(t);
        return this->decode_wrapped_group(tail, [this, &tail] {
            return std::apply([this](auto &&...args) { return this->applier(std::forward<decltype(args)>(args)...); }, tail);
        });
    }

    template <typename T>
        requires(IsAggregate<T> && !IsClassWithDecodingOverload<self_t, T>)
    constexpr status_code decode(T &value) {
        auto tuple = to_tuple(value);

        if constexpr (HasInlineTag<T>) {
            auto tag_scope = this->enter_tag(T::cbor_tag);
            if (!tag_scope) {
                return tag_scope.error();
            }
            return this->decode_wrapped_group(tuple, [this, &tuple] {
                return std::apply([this](auto &&...args) { return this->applier(std::forward<decltype(args)>(args)...); }, tuple);
            });
        } else if constexpr (IsTag<T>) {
            auto tag_scope = this->enter_tag(static_cast<std::uint64_t>(std::get<0>(tuple)));
            if (!tag_scope) {
                return tag_scope.error();
            }
            auto tail = detail::tuple_tail(tuple);
            return this->decode_wrapped_group(tail, [this, &tail] {
                return std::apply([this](auto &&...args) { return this->applier(std::forward<decltype(args)>(args)...); }, tail);
            });
        } else {
            return this->decode_wrapped_group(tuple, [this, &tuple] {
                return std::apply([this](auto &&...args) { return this->applier(std::forward<decltype(args)>(args)...); }, tuple);
            });
        }
    }

    template <typename T>
        requires(IsAggregate<T> && !IsClassWithDecodingOverload<self_t, T>)
    constexpr status_code decode(T &value, major_type major, byte additionalInfo) {
        if (major != major_type::Tag) {
            return status_code::no_match_for_tag_on_buffer;
        }

        auto tuple     = to_tuple(value);
        auto tag       = decode_unsigned(additionalInfo);
        auto tag_scope = enter_structural_item();
        if (!tag_scope) {
            return tag_scope.error();
        }
        return this->decode_tagged_aggregate(value, tag, tuple);
    }

    template <typename T>
        requires(IsAggregate<T> && !IsClassWithDecodingOverload<self_t, T>)
    constexpr status_code decode(T &value, std::uint64_t tag) {
        auto tuple = to_tuple(value);
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
            return this->decode_wrapped_group(tuple, [this, &tuple] {
                return std::apply([this](auto &&...args) { return this->applier(std::forward<decltype(args)>(args)...); }, tuple);
            });
        } else {
            if (tag != std::get<0>(tuple)) {
                return status_code::no_match_for_tag;
            }
            auto tail = detail::tuple_tail(tuple);
            return this->decode_wrapped_group(tail, [this, &tail] {
                return std::apply([this](auto &&...args) { return this->applier(std::forward<decltype(args)>(args)...); }, tail);
            });
        }
    }

    template <IsUntaggedTuple T> constexpr status_code decode(T &value) {
        return this->decode_wrapped_group(value, [this, &value] {
            return std::apply([this](auto &&...args) { return this->applier(std::forward<decltype(args)>(args)...); }, value);
        });
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
            auto t           = detail::make_decode_value_for_optional<value_type>(value);
            auto result      = decode(t, major, additionalInfo);
            if (result == status_code::success) {
                value = std::move(t);
            }
            return result;
        }
        return status_code::no_match_for_optional_on_buffer;
    }

    template <typename T> constexpr status_code decode(std::optional<T> &value, std::uint64_t tag) {
        using value_type = std::remove_cvref_t<T>;
        auto t           = detail::make_decode_value_for_optional<value_type>(value);
        auto result      = decode(t, tag);
        if (result == status_code::success) {
            value = std::move(t);
        }
        return result;
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

    template <typename... T> constexpr status_code decode(std::variant<T...> &value, major_type major, byte additionalInfo) {
        std::optional<std::uint64_t> tag;
        return decode_variant(value, major, additionalInfo, tag);
    }

    constexpr status_code decode(as_text_any &value, major_type major, byte additionalInfo) {
        if (major != major_type::TextString) {
            return status_code::no_match_for_tstr_on_buffer;
        }
        value.size = decode_unsigned(additionalInfo);
        return detail::skip_sized_string_payload(reader_, data_, value.size);
    }
    constexpr status_code decode(as_bstr_any &value, major_type major, byte additionalInfo) {
        if (major != major_type::ByteString) {
            return status_code::no_match_for_bstr_on_buffer;
        }
        value.size = decode_unsigned(additionalInfo);
        return detail::skip_sized_string_payload(reader_, data_, value.size);
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
        const auto status = detail::read_raw_encoded_item_bounds<InputBuffer, size_type, detail::max_decode_depth_option_v<Options>>(
            data_, start, expected_major, major_mismatch, bounds, structural_depth_);
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

        auto decode_payload = [this, &value]() constexpr -> status_code {
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
            return status_code::error;
        };

        // Automatic tag decoding - only performed if NOT in a variant context.
        if constexpr (DecodeTag && IsClassWithTagOverload<C>) {
            auto tag_scope = this->enter_tag(static_cast<std::uint64_t>(detail::get_major_6_tag_from_class(value)));
            if (!tag_scope) {
                return tag_scope.error();
            }
            return decode_payload();
        } else {
            return decode_payload();
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

    template <typename T> constexpr bool decode_root_argument(T &&arg, status_code &result) {
        result = decode(arg);
        return result == status_code::success;
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
                auto result      = detail::make_decode_value_for<value_type>(value);
                auto status      = decode(result, major, additionalInfo);
                if (status != status_code::success) {
                    return status;
                }
                appender_(value, std::move(result));
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

    template <typename T, typename DecodePayload> constexpr auto decode_wrapped_group(T &&, DecodePayload &&decode_payload) {
        using tuple_type     = std::decay_t<T>;
        constexpr auto size_ = std::tuple_size_v<tuple_type>;
        if constexpr (size_ > 1 && Options::wrap_groups) {
            auto group = this->enter_array(size_);
            if (!group) {
                return group.error();
            }
            return std::forward<DecodePayload>(decode_payload)();
        } else {
            return std::forward<DecodePayload>(decode_payload)();
        }
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
    std::size_t structural_depth_{};

    [[nodiscard]] constexpr expected<structural_scope, status_code> enter_structural_item() {
        if (structural_depth_ == detail::max_decode_depth_option_v<Options>) {
            return unexpected<status_code>(status_code::max_depth_exceeded);
        }
        ++structural_depth_;
        return structural_scope{*this};
    }

    template <typename Entered>
    [[nodiscard]] constexpr expected<Entered, status_code> enter_definite_container(major_type expected_major, status_code major_mismatch) {
        if (reader_.empty(data_)) {
            return unexpected<status_code>(status_code::incomplete);
        }

        const auto [major, additional_info] = read_initial_byte();
        if (major != expected_major) {
            return unexpected<status_code>(major_mismatch);
        }
        if (additional_info == static_cast<byte>(31)) {
            return unexpected<status_code>(status_code::unexpected_group_size);
        }

        const auto size  = decode_unsigned(additional_info);
        auto       scope = enter_structural_item();
        if (!scope) {
            return unexpected<status_code>(scope.error());
        }
        return Entered{size, std::move(*scope)};
    }

    template <typename... T>
    constexpr status_code decode_variant(std::variant<T...> &value, major_type major, byte additionalInfo,
                                         std::optional<std::uint64_t> &tag) {
        using namespace detail;
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

        structural_scope tag_scope{};
        if (major == major_type::Tag && !tag) {
            if (additionalInfo == static_cast<byte>(31)) {
                return status_code::no_match_for_tag;
            }
            tag        = decode_unsigned(additionalInfo);
            auto scope = enter_structural_item();
            if (!scope) {
                return scope.error();
            }
            tag_scope = std::move(*scope);
        }

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
            } else if constexpr (detail::is_optional_tag_v<raw_type> || IsTag<raw_type>) {
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
            } else if (major == major_type::Tag && result != status_code::no_match_for_tag &&
                       result != status_code::no_match_for_tag_on_buffer && result != status_code::no_match_in_variant_on_buffer) {
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
            auto depth_scope = dec.enter_structural_item();
            if (!depth_scope) {
                return depth_scope.error();
            }
            return dec.decode_indef_map(value.value_);
        } else if constexpr (IsArray<U> && !IsArrayHeader<U>) {
            if (major != major_type::Array || additionalInfo != static_cast<byte>(31)) {
                return status_code::no_match_for_array_on_buffer;
            }
            if constexpr (IsFixedArray<U>) {
                return status_code::unexpected_group_size;
            } else {
                auto depth_scope = dec.enter_structural_item();
                if (!depth_scope) {
                    return depth_scope.error();
                }
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
        auto group = detail::underlying<T>(this).enter_array(value.size_);
        if (!group) {
            if (group.error() == status_code::no_match_for_array_on_buffer) {
                return status_code::error;
            }
            return group.error();
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
