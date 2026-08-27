#pragma once

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_reflection_config.h"

#include <tuple>
#include <type_traits>

namespace cbor::tags::detail {

#if CBOR_TAGS_HAS_STD_REFLECTION || CBOR_TAGS_HAS_BOOST_PFR_NAMES

template <typename T>
    requires IsAggregate<T> || IsTuple<T>
constexpr auto aggregate_binding_count = []() consteval {
    using type = std::remove_cvref_t<T>;
    if constexpr (IsTuple<type>) {
        return std::tuple_size_v<type>;
#if CBOR_TAGS_HAS_STD_REFLECTION
    } else {
        return std::meta::nonstatic_data_members_of(^^type, std::meta::access_context::current()).size();
#else
    } else {
        return boost::pfr::tuple_size_v<type>;
#endif
    }
}();

#endif
} // namespace cbor::tags::detail
