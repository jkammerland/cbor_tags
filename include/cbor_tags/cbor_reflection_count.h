#pragma once

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_reflection_config.h"

#include <tuple>
#include <type_traits>

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

#endif

} // namespace cbor::tags::detail
