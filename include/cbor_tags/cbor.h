#pragma once

#include <tl/expected.hpp>

// Float 16, c++23 has std::float16_t from <stdfloat> maybe, for now use float16_t below
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_simple.h"
#include "cbor_tags/float16_ieee754.h"

#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace cbor::tags {

// Status/Error handling
enum class status : uint8_t {
    success = 0,
    incomplete,
    invalid_tag_for_simple,
    invalid_tag_for_optional,
    invalid_tag_value,
    invalid_major_type_for_unsigned_integer,
    invalid_major_type_for_negative_integer,
    invalid_major_type_for_integer,
    invalid_major_type_for_enum,
    invalid_major_type_for_binary_string,
    invalid_major_type_for_text_string,
    invalid_major_type_for_array,
    invalid_major_type_for_map,
    invalid_major_type_for_tag,
    invalid_major_type_for_simple,
    no_matching_tag_value_in_variant,
    invalid_container_size,
    out_of_memory,
    error
};

constexpr std::string_view status_to_message(status s) {
    switch (s) {
    case status::success: return "Success";
    case status::incomplete: return "Incomplete";
    case status::invalid_tag_for_simple: return "Invalid tag for simple";
    case status::invalid_tag_for_optional: return "Invalid tag for optional";
    case status::invalid_tag_value: return "Invalid tag value";
    case status::invalid_major_type_for_unsigned_integer: return "Invalid major type for unsigned integer";
    case status::invalid_major_type_for_negative_integer: return "Invalid major type for negative integer";
    case status::invalid_major_type_for_integer: return "Invalid major type for integer";
    case status::invalid_major_type_for_enum: return "Invalid major type for enum";
    case status::invalid_major_type_for_binary_string: return "Invalid major type for binary string";
    case status::invalid_major_type_for_text_string: return "Invalid major type for text string";
    case status::invalid_major_type_for_array: return "Invalid major type for array";
    case status::invalid_major_type_for_map: return "Invalid major type for map";
    case status::invalid_major_type_for_tag: return "Invalid major type for tag";
    case status::invalid_major_type_for_simple: return "Invalid major type for simple";
    case status::no_matching_tag_value_in_variant: return "No matching tag value in variant";
    case status::invalid_container_size: return "Invalid container size";
    case status::out_of_memory: return "Out of memory";
    case status::error: return "Error";
    default: return "Unknown status";
    }
}

template <typename T> struct Option {
    using is_options = void;
    using type       = T;
};

// TODO: use std::expected when available
template <typename T, typename E> using expected = tl::expected<T, status>;
template <typename E> using unexpected           = tl::unexpected<E>;
using default_expected                           = Option<expected<void, status>>;

template <typename V1, typename V2, typename T> struct values_equal : std::bool_constant<std::is_same_v<V1, V2>> {
    using type = T;
};

template <typename... T> struct ReturnTypeHelper {
    static auto get_return_type() {
        if constexpr (contains<Option<expected<void, status>>, T...>()) {
            return std::type_identity<expected<void, status>>{};
        } else if constexpr (contains<Option<expected<std::uint64_t, status>>, T...>()) {
            return std::type_identity<expected<std::uint64_t, status>>{};
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

template <std::ranges::input_range R> struct binary_range_view {
    R range;

    constexpr auto view() const {
        return range | std::views::transform([](const auto &c) { return static_cast<const std::byte>(c); });
    }
    constexpr auto begin() const {
        auto v = view();
        return std::ranges::begin(v);
    }
    constexpr auto end() const {
        auto v = view();
        return std::ranges::end(v);
    }

    operator std::vector<std::byte>() const { return {range.begin(), range.end()}; }
};

template <std::ranges::input_range R> struct char_range_view {
    R range;

    constexpr auto view() const {
        return range | std::views::transform([](const auto &c) { return static_cast<const char>(c); });
    }

    constexpr auto begin() const {
        auto v = view();
        return std::ranges::begin(v);
    }
    constexpr auto end() const {
        auto v = view();
        return std::ranges::end(v);
    }

    constexpr operator std::string() const { return {begin(), end()}; }
};

template <std::ranges::input_range R> struct binary_array_range_view {
    R range;
};

template <std::ranges::input_range R> struct binary_map_range_view {
    R range;
};

template <std::ranges::input_range R> struct binary_tag_range_view {
    std::uint64_t tag;
    R             range;
};

using variant_contiguous = std::variant<std::uint64_t, std::int64_t, std::span<const std::byte>, std::string_view, binary_array_view,
                                        binary_map_view, binary_tag_view, float16_t, float, double, bool, std::nullptr_t>;

template <typename R>
using variant_ranges = std::variant<std::uint64_t, std::int64_t, binary_range_view<R>, char_range_view<R>, binary_array_range_view<R>,
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
    std::tuple<T...> &&values_;
    std::uint64_t      size_{sizeof...(T)};
    constexpr wrap_as_array(T &&...values) : values_(values...) {}
};

struct as_indefinite_array {};
struct end_array {};

struct as_map {
    std::uint64_t size_;
    constexpr as_map(std::uint64_t size) : size_(size) {}
};

struct as_indefinite_map {};
struct end_map {};

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
        // std::unreachable(); // Due to IsCborMajor concept, this should never happen
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

} // namespace cbor::tags