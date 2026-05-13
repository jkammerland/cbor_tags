#pragma once

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_reflection_config.h"
#include "cbor_tags/cbor_reflection_count.h"

#include <cstddef>
#include <limits>
#include <tuple>
#include <type_traits>

#if CBOR_TAGS_HAS_BOOST_PFR_NAMES

namespace cbor::tags {

namespace detail {
constexpr std::size_t MAX_REFLECTION_MEMBERS = std::numeric_limits<std::size_t>::max() - 1;

template <typename T> consteval auto aggregate_member_count() { return boost::pfr::tuple_size_v<std::remove_cvref_t<T>>; }

template <typename T, std::size_t I> consteval auto aggregate_member_name() {
    return boost::pfr::get_name<I, std::remove_cvref_t<T>>();
}

} // namespace detail

template <class T> constexpr auto to_tuple(T &&object) noexcept {
    using type = std::remove_cvref_t<T>;
    static_assert(IsAggregate<type>, "Type must be an aggregate");

    if constexpr (std::is_rvalue_reference_v<T &&>) {
        return boost::pfr::structure_to_tuple(object);
    } else {
        return boost::pfr::structure_tie(object);
    }
}

} // namespace cbor::tags

#include "cbor_tags/cbor_reflection_named.h"

#endif
