#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_concepts_checking.h"

#include <cstddef>
#include <type_traits>
#include <variant>

namespace cbor::tags::detail {

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

template <typename U> constexpr bool matches_major_dispatch(major_type major) {
    using type = std::remove_cvref_t<U>;
    if constexpr (IsOptional<type>) {
        return major == major_type::Simple || matches_major_dispatch<typename type::value_type>(major);
    } else if constexpr (IsVariant<type>) {
        return []<typename... Ts>(std::variant<Ts...> *, major_type m) {
            return (matches_major_dispatch<Ts>(m) || ...);
        }(static_cast<type *>(nullptr), major);
    } else {
        return is_valid_major<major_type, type>(major);
    }
}

} // namespace cbor::tags::detail
