#pragma once

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_reflection_config.h"

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace cbor::tags::detail {

#if CBOR_TAGS_HAS_STD_REFLECTION

template <typename T>
    requires IsAggregate<T> || IsTuple<T>
constexpr auto aggregate_binding_count = [] consteval {
    using type = std::remove_cvref_t<T>;
    if constexpr (IsTuple<type>) {
        return std::tuple_size_v<type>;
    } else {
        return std::meta::nonstatic_data_members_of(^^type, std::meta::access_context::current()).size();
    }
}();

#else

template <typename T, typename... TArgs>
    requires IsAggregate<T> || IsTuple<T>
constexpr std::size_t num_bindings_impl() {
    if constexpr (requires { T{std::declval<TArgs>()...}; }) {
        return num_bindings_impl<T, any, TArgs...>();
    } else {
        return sizeof...(TArgs) - 1;
    }
}

template <typename T>
    requires IsAggregate<T> || IsTuple<T>
constexpr auto aggregate_binding_count = num_bindings_impl<T, any>();

#endif
} // namespace cbor::tags::detail
