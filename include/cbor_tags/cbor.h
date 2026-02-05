#pragma once

#include <tl/expected.hpp>

// Float 16, c++23 has std::float16_t from <stdfloat> maybe, for now use float16_t below
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_simple.h"
#include "cbor_tags/float16_ieee754.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cbor::tags {

// Status/Error handling
enum class status_code : uint8_t {
    success = 0,
    incomplete,
    unexpected_group_size,
    out_of_memory,
    error,
    contiguous_view_on_non_contiguous_data,
    invalid_utf8_sequence,
    begin_no_match_decoding,
    no_match_for_tag,
    no_match_for_tag_simple_on_buffer,
    no_match_for_uint_on_buffer,
    no_match_for_nint_on_buffer,
    no_match_for_int_on_buffer,
    no_match_for_enum_on_buffer,
    no_match_for_bstr_on_buffer,
    no_match_for_tstr_on_buffer,
    no_match_for_array_on_buffer,
    no_match_for_map_on_buffer,
    no_match_for_tag_on_buffer,
    no_match_for_simple_on_buffer,
    no_match_for_optional_on_buffer,
    no_match_in_variant_on_buffer,
    end_no_match_decoding
};

constexpr std::string_view status_message(status_code s) {
    switch (s) {
    case status_code::success: return "Success";
    case status_code::incomplete: return "Unexpected end of CBOR data: buffer incomplete";
    case status_code::unexpected_group_size: return "Unexpected group size in CBOR data(e.g array or map size mismatch)";
    case status_code::out_of_memory: return "Unexpected memory allocation failure during CBOR processing";
    case status_code::error: return "Unexpected CBOR processing error";
    case status_code::contiguous_view_on_non_contiguous_data: return "Attempt to create a contiguous view on non-contiguous data";
    case status_code::invalid_utf8_sequence: return "Invalid UTF-8 sequence in text string";
    case status_code::begin_no_match_decoding: return "Unexpected error at start of CBOR decoding: invalid initial byte";
    case status_code::no_match_for_tag: return "Unexpected CBOR tag: no matching decoder found, incase of dynamic tags, they must be correctly assigned before decoding(or encoding)";
    case status_code::no_match_for_tag_simple_on_buffer: return "Unexpected CBOR simple value tag: no matching decoder found";
    case status_code::no_match_for_uint_on_buffer: return "Unexpected value for CBOR major type 0: unsigned integer decode failed";
    case status_code::no_match_for_nint_on_buffer: return "Unexpected value for CBOR major type 1: negative integer decode failed";
    case status_code::no_match_for_int_on_buffer: return "Unexpected integer value in CBOR data: decode failed";
    case status_code::no_match_for_enum_on_buffer: return "Unexpected enum value in CBOR data: no matching enum constant";
    case status_code::no_match_for_bstr_on_buffer: return "Unexpected value for CBOR major type 2: byte string decode failed";
    case status_code::no_match_for_tstr_on_buffer: return "Unexpected value for CBOR major type 3: text string decode failed";
    case status_code::no_match_for_array_on_buffer: return "Unexpected value for CBOR major type 4: array decode failed";
    case status_code::no_match_for_map_on_buffer: return "Unexpected value for CBOR major type 5: incorrect major type for map";
    case status_code::no_match_for_tag_on_buffer: return "Unexpected value for CBOR major type 6: incorrect major type for tag";
    case status_code::no_match_for_simple_on_buffer: return "Unexpected value for CBOR major type 7: simple value decode failed";
    case status_code::no_match_for_optional_on_buffer: return "Unexpected CBOR format: optional value decode failed";
    case status_code::no_match_in_variant_on_buffer: return "Unexpected CBOR format: no matching variant type found";
    case status_code::end_no_match_decoding: return "Unexpected error at end of CBOR decoding: invalid terminal state";
    default: return "Unknown CBOR status code";
    }
}

template <typename T> struct Option {
    using is_options = void;
    using type       = T;
};

// TODO: use std::expected when available
template <typename T, typename E> using expected = tl::expected<T, status_code>;
template <typename E> using unexpected           = tl::unexpected<E>;
using default_expected                           = Option<expected<void, status_code>>;

namespace detail {
struct wrap_groups {};
}; // namespace detail

using default_wrapping = Option<detail::wrap_groups>;

template <typename V1, typename V2, typename T> struct values_equal : std::bool_constant<std::is_same_v<V1, V2>> {
    using type = T;
};

template <typename... T> struct ReturnTypeHelper {
    static auto get_return_type() {
        if constexpr (contains<Option<expected<void, status_code>>, T...>()) {
            return std::type_identity<expected<void, status_code>>{};
        } else if constexpr (contains<Option<expected<std::uint64_t, status_code>>, T...>()) {
            return std::type_identity<expected<std::uint64_t, status_code>>{};
        } else {
            return std::type_identity<void>{};
        }
    }
    using type = typename decltype(get_return_type())::type;
};

template <typename... T> struct Options {
    using is_options  = void;
    using return_type = typename ReturnTypeHelper<T...>::type;
    using error_type  = typename ReturnTypeHelper<T...>::type::error_type;

    // When false, a tagged type or tuple of multiple items will not be wrapped in an array by default
    static constexpr bool wrap_groups = contains<default_wrapping, T...>();

    constexpr Options() = default;
};
// ---------

struct binary_array_view {
    std::span<const std::byte> data;
};

struct binary_map_view {
    std::span<const std::byte> data;
};

struct binary_tag_view {
    std::uint64_t              tag;
    std::span<const std::byte> data;
};

template <std::ranges::input_range R> struct bstr_view : std::ranges::view_interface<bstr_view<R>> {
    using value_type   = std::byte;
    using element_type = const std::byte;

    R range;

    constexpr auto begin() const {
        auto v = view();
        return std::ranges::begin(v);
    }
    constexpr auto end() const {
        auto v = view();
        return std::ranges::end(v);
    }

    operator std::vector<std::byte>() const {
        auto v = view();
        return {v.begin(), v.end()};
    }

    constexpr auto view() const {
        return range | std::views::transform([](const auto &c) { return static_cast<element_type>(c); });
    }
};

template <std::ranges::input_range R> struct tstr_view : std::ranges::view_interface<tstr_view<R>> {
    using value_type   = char;
    using element_type = const char;

    R range;

    constexpr auto begin() const {
        auto v = view();
        return std::ranges::begin(v);
    }
    constexpr auto end() const {
        auto v = view();
        return std::ranges::end(v);
    }

    constexpr operator std::string() const {
        auto v = view();
        return {v.begin(), v.end()};
    }

    constexpr auto view() const {
        return range | std::views::transform([](const auto &c) { return static_cast<element_type>(c); });
    }
};

// TODO: Not implemented! This is not the way I think.
template <std::ranges::input_range R> struct binary_array_range_view {
    R range;
};

// TODO: Not implemented!
template <std::ranges::input_range R> struct binary_map_range_view {
    R range;
};

// TODO: Not implemented!
template <std::ranges::input_range R> struct binary_tag_range_view {
    std::uint64_t tag;
    R             range;
};

using variant_contiguous = std::variant<std::uint64_t, std::int64_t, std::span<const std::byte>, std::string_view, binary_array_view,
                                        binary_map_view, binary_tag_view, float16_t, float, double, bool, std::nullptr_t>;

template <typename R>
using variant_ranges = std::variant<std::uint64_t, std::int64_t, bstr_view<R>, tstr_view<R>, binary_array_range_view<R>,
                                    binary_map_range_view<R>, binary_tag_range_view<R>, float16_t, float, double, bool, std::nullptr_t>;

template <typename T> using subrange  = std::ranges::subrange<typename detail::iterator_type<T>::type>;
template <typename T> using variant_t = std::conditional_t<IsContiguous<T>, variant_contiguous, variant_ranges<subrange<T>>>;

template <typename Tag, typename T> using tagged_object = std::pair<Tag, T>;
template <typename Tag, typename T> constexpr auto make_tag_pair(Tag t, T &&value) {
    return tagged_object<Tag, T>{t, std::forward<T>(value)};
}

struct as_indefinite_text_string {};
struct as_indefinite_byte_string {};
struct end_string {};

struct as_array {
    std::uint64_t size_;
    constexpr as_array(std::uint64_t size) : size_(size) {}
};

template <typename... T> struct wrap_as_array {
    std::tuple<T &&...> values_;
    std::uint64_t       size_{sizeof...(T)};

    constexpr explicit wrap_as_array(T &&...values) : values_(std::forward<T>(values)...) {}
};

// Specialization for when a single tuple is passed
template <typename... TupleArgs> struct wrap_as_array<std::tuple<TupleArgs...>> {
    std::tuple<TupleArgs...> &values_;
    std::uint64_t             size_{sizeof...(TupleArgs)};

    constexpr explicit wrap_as_array(std::tuple<TupleArgs...> &tuple) : values_(tuple) {}
};

// Deduction guide for regular arguments
template <typename... T> wrap_as_array(T &&...) -> wrap_as_array<T &&...>;

// Deduction guide for tuple
template <typename... TupleArgs> wrap_as_array(std::tuple<TupleArgs...> &) -> wrap_as_array<std::tuple<TupleArgs...>>;

struct as_indefinite_array {};
struct end_array {};

struct as_map {
    std::uint64_t size_;
    constexpr as_map(std::uint64_t size) : size_(size) {}
};

struct as_indefinite_map {};
struct end_map {};

template <typename T> struct as_indefinite {
    T &value_;
    constexpr explicit as_indefinite(T &value) : value_(value) {}
};

template <typename T> as_indefinite(T &) -> as_indefinite<T>;

template <typename T> struct as_maybe_indefinite {
    T value_;
    constexpr as_maybe_indefinite() = default;
    constexpr explicit as_maybe_indefinite(T value) : value_(std::move(value)) {}
    constexpr T       &get() noexcept { return value_; }
    constexpr const T &get() const noexcept { return value_; }
};

template <typename T> struct as_maybe_indefinite<T &> {
    T *value_;
    constexpr explicit as_maybe_indefinite(T &value) : value_(std::addressof(value)) {}
    constexpr T &get() const noexcept { return *value_; }
};

template <typename T> using as_maybe_indefinite_ref   = as_maybe_indefinite<T &>;
template <typename T> using as_maybe_indefinite_value = as_maybe_indefinite<T>;

template <typename T> as_maybe_indefinite(T &) -> as_maybe_indefinite<T &>;
template <typename T> as_maybe_indefinite(T &&) -> as_maybe_indefinite<std::remove_cvref_t<T>>;

// Compile-time function to get CBOR major type
template <IsCborMajor T> constexpr std::byte get_major_3_bit_tag() {
    if constexpr (IsUnsigned<T>) {
        return static_cast<std::byte>(0x00);
    } else if constexpr (IsNegative<T>) {
        return static_cast<std::byte>(0x20);
    } else if constexpr (IsBinaryString<T>) {
        return static_cast<std::byte>(0x40);
    } else if constexpr (IsTextString<T>) {
        return static_cast<std::byte>(0x60);
    } else if constexpr (IsArray<T>) {
        return static_cast<std::byte>(0x80);
    } else if constexpr (IsMap<T>) {
        return static_cast<std::byte>(0xA0);
    } else if constexpr (IsTag<T>) {
        return static_cast<std::byte>(0xC0);
    } else if constexpr (IsSimple<T>) {
        return static_cast<std::byte>(0xE0);
    } else {
        return static_cast<std::byte>(0xFF);
    }
}
// Compile-time function to get CBOR simple type 5-bit value
template <IsSimple T> constexpr std::byte get_simple_5_bit_value() {
    if constexpr (IsBool<T>) {
        return static_cast<std::byte>(0x14); // false is 20, true is handled separately
    } else if constexpr (IsNull<T>) {
        return static_cast<std::byte>(0x16); // null is 22
    } else if constexpr (std::is_same_v<T, simple>) {
        return static_cast<std::byte>(0x18); // simple is 24 (read next byte)
    } else if constexpr (IsFloat16<T>) {
        return static_cast<std::byte>(0x19); // float16 is 25 (read next 2 bytes)
    } else if constexpr (IsFloat32<T>) {
        return static_cast<std::byte>(0x1A); // float32 is 26 (read next 4 bytes)
    } else if constexpr (IsFloat64<T>) {
        return static_cast<std::byte>(0x1B); // float64 is 27 (read next 8 bytes)
    } else if constexpr (std::is_same_v<T, end_string>) {
        return static_cast<std::byte>(0xFF); // end of string
    } else if constexpr (std::is_same_v<T, end_array>) {
        return static_cast<std::byte>(0xFF); // end of array
    } else if constexpr (std::is_same_v<T, end_map>) {
        return static_cast<std::byte>(0xFF); // end of map
    } else {
        return static_cast<std::byte>(0x00);
    }
}
enum class SimpleType : std::uint8_t {
    Undefined  = 0x00,
    Bool_False = 0x14,
    Bool_True  = 0x15,
    Null       = 0x16,
    Simple     = 0x18,
    Float16    = 0x19,
    Float32    = 0x1A,
    Float64    = 0x1B,
    End_Marker = 0xFF // Used for end_string, end_array, and end_map
};

template <IsSimple T> constexpr bool compare_simple_value(std::byte value) {
    if constexpr (IsBool<T>) {
        return value == static_cast<std::byte>(SimpleType::Bool_False) || value == static_cast<std::byte>(SimpleType::Bool_True);
    } else if constexpr (IsNull<T>) {
        return value == static_cast<std::byte>(SimpleType::Null);
    } else if constexpr (std::is_same_v<T, simple>) {
        return value == static_cast<std::byte>(SimpleType::Simple);
    } else if constexpr (IsFloat16<T>) {
        return value == static_cast<std::byte>(SimpleType::Float16);
    } else if constexpr (IsFloat32<T>) {
        return value == static_cast<std::byte>(SimpleType::Float32);
    } else if constexpr (IsFloat64<T>) {
        return value == static_cast<std::byte>(SimpleType::Float64);
    } else if constexpr (std::is_same_v<T, end_string> || std::is_same_v<T, end_array> || std::is_same_v<T, end_map>) {
        return value == static_cast<std::byte>(SimpleType::End_Marker);
    }
    return false;
}

template <IsSimple T> constexpr SimpleType get_simple_tag_of_primitive_type() {
    if constexpr (IsBool<T>) {
        return SimpleType::Bool_False;
    } else if constexpr (IsNull<T>) {
        return SimpleType::Null;
    } else if constexpr (std::is_same_v<T, simple>) {
        return SimpleType::Simple;
    } else if constexpr (IsFloat16<T>) {
        return SimpleType::Float16;
    } else if constexpr (IsFloat32<T>) {
        return SimpleType::Float32;
    } else if constexpr (IsFloat64<T>) {
        return SimpleType::Float64;
    } else if constexpr (std::is_same_v<T, end_string> || std::is_same_v<T, end_array> || std::is_same_v<T, end_map>) {
        return SimpleType::End_Marker;
    } else {
        return SimpleType::Undefined;
    }
}

// Compile-time function to get CBOR start value for indefinite
template <typename T> constexpr std::byte get_indefinite_start() {
    if constexpr (std::is_same_v<T, as_indefinite_text_string>) {
        return static_cast<std::byte>(0x7F);
    } else if constexpr (std::is_same_v<T, as_indefinite_byte_string>) {
        return static_cast<std::byte>(0x5F);
    } else if constexpr (std::is_same_v<T, as_indefinite_array>) {
        return static_cast<std::byte>(0x9F);
    } else if constexpr (std::is_same_v<T, as_indefinite_map>) {
        return static_cast<std::byte>(0xBF);
    } else {
        return static_cast<std::byte>(0x00);
    }
}

struct parse_integer_exception : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct parse_simple_exception : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct parse_incomplete_exception : std::runtime_error {
    using std::runtime_error::runtime_error;
};

} // namespace cbor::tags
