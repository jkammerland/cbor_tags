#pragma once

#include "cbor_tags/cbor_concepts.h"

#include <type_traits>

namespace cbor::tags::detail {

template <typename T, auto Matches> consteval bool has_matching_alternative() {
    using type = std::remove_cvref_t<T>;
    if constexpr (Matches.template operator()<type>()) {
        return true;
    } else if constexpr (IsOptional<type>) {
        return has_matching_alternative<typename type::value_type, Matches>();
    } else if constexpr (IsVariant<type>) {
        return with_variant_alternatives<type>([]<typename... Ts>() { return (has_matching_alternative<Ts, Matches>() || ...); });
    } else {
        return false;
    }
}

} // namespace cbor::tags::detail
