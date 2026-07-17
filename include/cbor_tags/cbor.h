#pragma once

#if __has_include("cbor_tags/cbor_tags_config.h")
#include "cbor_tags/cbor_tags_config.h"
#endif

#ifndef CBOR_TAGS_STL_ONLY
#define CBOR_TAGS_STL_ONLY 0
#endif

#ifndef CBOR_TAGS_USE_STD_EXPECTED
#define CBOR_TAGS_USE_STD_EXPECTED CBOR_TAGS_STL_ONLY
#endif

#if CBOR_TAGS_USE_STD_EXPECTED
#include <expected>
#if __has_include(<version>)
#include <version>
#endif
#if !defined(__cpp_lib_expected) || __cpp_lib_expected < 202202L
#error "CBOR_TAGS_USE_STD_EXPECTED requires C++23 <expected> with __cpp_lib_expected >= 202202L"
#endif
#else
#include <tl/expected.hpp>
#endif

// Float 16, c++23 has std::float16_t from <stdfloat> maybe, for now use float16_t below
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_ranges.h"
#include "cbor_tags/cbor_raw_views.h"
#include "cbor_tags/cbor_simple.h"
#include "cbor_tags/float16_ieee754.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
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
    end_no_match_decoding,
    size_limit_exceeded
};

template <typename Self> struct cbor_encoder_mixin_base {
    constexpr void encode() = delete;
};

template <typename Self> struct cbor_decoder_mixin_base {
    constexpr status_code decode() = delete;
};

template <typename Self> struct cbor_codec_mixin_base : cbor_encoder_mixin_base<Self>, cbor_decoder_mixin_base<Self> {
    using cbor_decoder_mixin_base<Self>::decode;
    using cbor_encoder_mixin_base<Self>::encode;
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
    case status_code::no_match_for_tag:
        return "Unexpected CBOR tag: no matching decoder found, incase of dynamic tags, they must be correctly assigned before decoding(or "
               "encoding)";
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
    case status_code::size_limit_exceeded: return "CBOR item size limit exceeded";
    default: return "Unknown CBOR status code";
    }
}

template <typename T> struct Option {
    using is_options = void;
    using type       = T;
};

#if CBOR_TAGS_USE_STD_EXPECTED
template <typename T, typename E> using expected = std::expected<T, E>;
template <typename E> using unexpected           = std::unexpected<E>;
#else
template <typename T, typename E> using expected = tl::expected<T, E>;
template <typename E> using unexpected           = tl::unexpected<E>;
#endif
using default_expected = Option<expected<void, status_code>>;

namespace detail {
struct wrap_groups {};
struct strict_integer_decode {};
}; // namespace detail

using default_wrapping        = Option<detail::wrap_groups>;
using strict_integer_decoding = Option<detail::strict_integer_decode>;

template <typename V1, typename V2, typename T> struct values_equal : std::bool_constant<std::is_same_v<V1, V2>> {
    using type = T;
};

template <typename T, std::size_t Min, std::size_t Max> struct bounded_size {
    static_assert(Min <= Max, "bounded_size<T, Min, Max> requires Min <= Max");
    static_assert(!IsAnyBoundedSizeWrapper<T>, "bounded size wrappers cannot directly contain another bounded size wrapper");

    using value_type = std::remove_reference_t<T>;

    static constexpr std::size_t min_size = Min;
    static constexpr std::size_t max_size = Max;

    T value_{};

    constexpr bounded_size()
        requires(!std::is_reference_v<T> && std::default_initializable<T>)
    = default;

    constexpr explicit bounded_size(const value_type &value)
        requires(!std::is_reference_v<T> && std::copy_constructible<value_type>)
        : value_(value) {}

    constexpr explicit bounded_size(value_type &&value)
        requires(!std::is_reference_v<T> && std::move_constructible<value_type>)
        : value_(std::move(value)) {}

    constexpr explicit bounded_size(value_type &value)
        requires(std::is_lvalue_reference_v<T>)
        : value_(value) {}

    [[nodiscard]] constexpr decltype(auto) value() & noexcept { return (value_); }
    [[nodiscard]] constexpr decltype(auto) value() const & noexcept { return (value_); }
    [[nodiscard]] constexpr decltype(auto) value() && noexcept {
        if constexpr (std::is_reference_v<T>) {
            return (value_);
        } else {
            return std::move(value_);
        }
    }
    [[nodiscard]] constexpr decltype(auto) value() const && noexcept {
        if constexpr (std::is_reference_v<T>) {
            return (value_);
        } else {
            return std::move(value_);
        }
    }
};

template <typename T> struct dynamic_bounded_size {
    static_assert(!IsAnyBoundedSizeWrapper<T>, "bounded size wrappers cannot directly contain another bounded size wrapper");

    using value_type = std::remove_reference_t<T>;

  private:
    [[nodiscard]] static constexpr std::size_t checked_max(std::size_t min, std::size_t max) {
        if (min > max) {
            throw std::invalid_argument("dynamic_bounded_size<T> requires min <= max");
        }
        return max;
    }

    std::size_t min_size_;
    std::size_t max_size_;

  public:
    T value_;

    constexpr explicit dynamic_bounded_size(const value_type &value, std::size_t min, std::size_t max)
        requires(!std::is_reference_v<T> && std::copy_constructible<value_type>)
        : min_size_(min), max_size_(checked_max(min, max)), value_(value) {}

    constexpr explicit dynamic_bounded_size(value_type &&value, std::size_t min, std::size_t max)
        requires(!std::is_reference_v<T> && std::move_constructible<value_type>)
        : min_size_(min), max_size_(checked_max(min, max)), value_(std::move(value)) {}

    constexpr explicit dynamic_bounded_size(value_type &value, std::size_t min, std::size_t max)
        requires(std::is_lvalue_reference_v<T>)
        : min_size_(min), max_size_(checked_max(min, max)), value_(value) {}

    [[nodiscard]] constexpr std::size_t min_size() const noexcept { return min_size_; }
    [[nodiscard]] constexpr std::size_t max_size() const noexcept { return max_size_; }

    [[nodiscard]] constexpr decltype(auto) value() & noexcept { return (value_); }
    [[nodiscard]] constexpr decltype(auto) value() const & noexcept { return (value_); }
    [[nodiscard]] constexpr decltype(auto) value() && noexcept {
        if constexpr (std::is_reference_v<T>) {
            return (value_);
        } else {
            return std::move(value_);
        }
    }
    [[nodiscard]] constexpr decltype(auto) value() const && noexcept {
        if constexpr (std::is_reference_v<T>) {
            return (value_);
        } else {
            return std::move(value_);
        }
    }
};

template <typename T, std::size_t Max> using max_size = bounded_size<T, 0, Max>;
template <typename T, std::size_t N> using exact_size = bounded_size<T, N, N>;

namespace detail {

template <typename T> [[nodiscard]] constexpr decltype(auto) unwrap_bounded_size(T &&value) {
    if constexpr (IsAnyBoundedSizeWrapper<T>) {
        return unwrap_bounded_size(std::forward<T>(value).value());
    } else {
        return std::forward<T>(value);
    }
}

template <typename T> using bounded_stored_type_t = std::conditional_t<std::is_lvalue_reference_v<T>, T, std::remove_cvref_t<T>>;

[[nodiscard]] constexpr status_code bounded_size_status(std::uint64_t size, std::size_t min, std::size_t max) noexcept {
    return size >= static_cast<std::uint64_t>(min) && size <= static_cast<std::uint64_t>(max) ? status_code::success
                                                                                              : status_code::size_limit_exceeded;
}

} // namespace detail

template <std::size_t Min, std::size_t Max, typename T> [[nodiscard]] constexpr auto as_bounded_size(T &&value) {
    auto &&unwrapped  = detail::unwrap_bounded_size(std::forward<T>(value));
    using stored_type = detail::bounded_stored_type_t<decltype(unwrapped)>;
    return bounded_size<stored_type, Min, Max>{std::forward<decltype(unwrapped)>(unwrapped)};
}

template <typename T> [[nodiscard]] constexpr auto as_bounded_size(T &&value, std::size_t min, std::size_t max) {
    auto &&unwrapped  = detail::unwrap_bounded_size(std::forward<T>(value));
    using stored_type = detail::bounded_stored_type_t<decltype(unwrapped)>;
    return dynamic_bounded_size<stored_type>{std::forward<decltype(unwrapped)>(unwrapped), min, max};
}

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
    // When true, decoding a CBOR integer into a narrower native integer target rejects instead of slicing.
    static constexpr bool strict_integer_decode = contains<strict_integer_decoding, T...>();

    constexpr Options() = default;
};

using default_options                = Options<default_expected, default_wrapping>;
using strict_integer_decoder_options = Options<default_expected, default_wrapping, strict_integer_decoding>;
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

template <typename Iterator, typename Value> struct cast_view_iterator {
    Iterator it{};

    using value_type      = Value;
    using difference_type = std::ptrdiff_t;
    using iterator_category =
        std::conditional_t<std::bidirectional_iterator<Iterator>, std::bidirectional_iterator_tag,
                           std::conditional_t<std::forward_iterator<Iterator>, std::forward_iterator_tag, std::input_iterator_tag>>;
    using iterator_concept = iterator_category;

    constexpr value_type          operator*() const { return static_cast<value_type>(*it); }
    constexpr cast_view_iterator &operator++() {
        ++it;
        return *this;
    }
    constexpr cast_view_iterator operator++(int) {
        auto copy = *this;
        ++(*this);
        return copy;
    }
    constexpr cast_view_iterator &operator--()
        requires std::bidirectional_iterator<Iterator>
    {
        --it;
        return *this;
    }
    constexpr cast_view_iterator operator--(int)
        requires std::bidirectional_iterator<Iterator>
    {
        auto copy = *this;
        --(*this);
        return copy;
    }

    friend bool operator==(const cast_view_iterator &lhs, const cast_view_iterator &rhs) { return lhs.it == rhs.it; }
};

template <std::ranges::input_range R>
    requires std::ranges::common_range<R>
struct bstr_view : std::ranges::view_interface<bstr_view<R>> {
    using value_type   = std::byte;
    using element_type = const std::byte;
    using iterator     = cast_view_iterator<std::ranges::iterator_t<const R>, value_type>;

    R range;

    constexpr bstr_view()
        requires std::default_initializable<R>
    = default;
    constexpr explicit bstr_view(R input_range) : range(std::move(input_range)) {}

    constexpr auto begin() const { return iterator{std::ranges::begin(range)}; }
    constexpr auto end() const { return iterator{std::ranges::end(range)}; }
    constexpr auto size() const
        requires std::ranges::sized_range<const R>
    {
        return std::ranges::size(range);
    }

    operator std::vector<std::byte>() const { return {begin(), end()}; }

    constexpr auto view() const { return std::ranges::subrange(begin(), end()); }
};

template <std::ranges::input_range R>
    requires std::ranges::common_range<R>
struct tstr_view : std::ranges::view_interface<tstr_view<R>> {
    using value_type   = char;
    using element_type = const char;
    using iterator     = cast_view_iterator<std::ranges::iterator_t<const R>, value_type>;

    R range;

    constexpr tstr_view()
        requires std::default_initializable<R>
    = default;
    constexpr explicit tstr_view(R input_range) : range(std::move(input_range)) {}

    constexpr auto begin() const { return iterator{std::ranges::begin(range)}; }
    constexpr auto end() const { return iterator{std::ranges::end(range)}; }

    constexpr operator std::string() const { return {begin(), end()}; }

    constexpr auto view() const { return std::ranges::subrange(begin(), end()); }
};

using variant_contiguous = std::variant<std::uint64_t, std::int64_t, std::span<const std::byte>, std::string_view, binary_array_view,
                                        binary_map_view, binary_tag_view, float16_t, float, double, bool, std::nullptr_t>;

template <typename R>
using variant_ranges =
    std::variant<std::uint64_t, std::int64_t, bstr_view<R>, tstr_view<R>, float16_t, float, double, bool, std::nullptr_t>;

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

template <typename T> struct as_named_map {
    T &value_;
    constexpr explicit as_named_map(T &value) : value_(value) {}
};

template <typename T> as_named_map(T &) -> as_named_map<T>;
template <typename T> as_named_map(const T &) -> as_named_map<const T>;

template <typename T> struct as_named_group {
    using value_type = T;
    T value_{};

    constexpr as_named_group() = default;
    constexpr explicit as_named_group(const T &value) : value_(value) {}
    constexpr explicit as_named_group(T &&value) : value_(std::move(value)) {}
};

template <typename T> as_named_group(T) -> as_named_group<T>;

template <typename T> struct as_named_extension {
    using value_type = T;
    T value_{};

    constexpr as_named_extension() = default;
    constexpr explicit as_named_extension(const T &value) : value_(value) {}
    constexpr explicit as_named_extension(T &&value) : value_(std::move(value)) {}
};

template <typename T> as_named_extension(T) -> as_named_extension<T>;

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
    } else if constexpr (std::is_same_v<T, end_string> || std::is_same_v<T, end_array> || std::is_same_v<T, end_map>) {
        return static_cast<std::byte>(0xFF); // break marker
    } else {
        return static_cast<std::byte>(0x00);
    }
}
enum class SimpleType : std::uint8_t {
    Unmatched  = 0xFE,
    Bool_False = 0x14,
    Bool_True  = 0x15,
    Null       = 0x16,
    Undefined  = 0x17,
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
        const auto additional_info = std::to_integer<std::uint8_t>(value);
        return additional_info < static_cast<std::uint8_t>(SimpleType::Bool_False) ||
               additional_info == static_cast<std::uint8_t>(SimpleType::Undefined) ||
               additional_info == static_cast<std::uint8_t>(SimpleType::Simple);
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
        return SimpleType::Unmatched;
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
