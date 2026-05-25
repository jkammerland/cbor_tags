#pragma once

#include "cbor_tags/cbor_reflection_config.h"

#include <string_view>
#include <type_traits>

#ifndef CBOR_TAGS_STL_ONLY
#define CBOR_TAGS_STL_ONLY 0
#endif

#if !CBOR_TAGS_STL_ONLY
#include <nameof.hpp>
#endif

namespace cbor::tags::detail {

template <typename T> constexpr std::string_view short_type_name() {
#if CBOR_TAGS_HAS_STD_REFLECTION
    if constexpr (!std::is_same_v<T, std::remove_cvref_t<T>>) {
        return short_type_name<std::remove_cvref_t<T>>();
    } else if constexpr (std::meta::has_identifier(^^T)) {
        return std::meta::identifier_of(^^T);
    } else {
        return std::meta::display_string_of(^^T);
    }
#else
    static_assert(!CBOR_TAGS_STL_ONLY, "CBOR_TAGS_STL_ONLY requires C++26 std::meta reflection for type names");
    return nameof::nameof_short_type<T>();
#endif
}

template <typename T> constexpr std::string_view full_type_name() {
#if CBOR_TAGS_HAS_STD_REFLECTION
    if constexpr (!std::is_same_v<T, std::remove_cvref_t<T>>) {
        return full_type_name<std::remove_cvref_t<T>>();
    } else {
        return std::meta::display_string_of(^^T);
    }
#else
    static_assert(!CBOR_TAGS_STL_ONLY, "CBOR_TAGS_STL_ONLY requires C++26 std::meta reflection for type names");
    return nameof::nameof_full_type<T>();
#endif
}

} // namespace cbor::tags::detail
