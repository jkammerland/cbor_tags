#pragma once

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_reflection_config.h"
#include "cbor_tags/cbor_reflection_count.h"

#include <cstddef>
#include <limits>
#include <tuple>
#include <type_traits>
#include <utility>

#if CBOR_TAGS_HAS_BOOST_PFR_NAMES

namespace cbor::tags {

namespace detail {
constexpr std::size_t MAX_REFLECTION_MEMBERS = std::numeric_limits<std::size_t>::max() - 1;

template <typename T> consteval auto aggregate_member_count() { return boost::pfr::tuple_size_v<std::remove_cvref_t<T>>; }

template <typename T, std::size_t I> consteval auto aggregate_member_name() { return boost::pfr::get_name<I, std::remove_cvref_t<T>>(); }

template <typename T, std::size_t... Is>
constexpr auto
pfr_structure_to_tuple(T &&object,
                       std::index_sequence<Is...>) noexcept(noexcept(std::make_tuple(boost::pfr::get<Is>(std::forward<T>(object))...))) {
    return std::make_tuple(boost::pfr::get<Is>(std::forward<T>(object))...);
}

template <typename T> constexpr bool pfr_to_tuple_noexcept() {
    using type = std::remove_cvref_t<T>;
    if constexpr (!IsAggregate<type>) {
        return false;
    } else if constexpr (std::is_rvalue_reference_v<T &&>) {
        return noexcept(pfr_structure_to_tuple(std::declval<T>(), std::make_index_sequence<aggregate_member_count<type>()>{}));
    } else {
        return noexcept(boost::pfr::structure_tie(std::declval<T>()));
    }
}

template <typename T> constexpr auto pfr_to_tuple(T &&object) noexcept(pfr_to_tuple_noexcept<T>()) {
    using type = std::remove_cvref_t<T>;
    if constexpr (std::is_rvalue_reference_v<T &&>) {
        return pfr_structure_to_tuple(std::forward<T>(object), std::make_index_sequence<aggregate_member_count<type>()>{});
    } else {
        return boost::pfr::structure_tie(object);
    }
}

} // namespace detail

template <class T> constexpr auto to_tuple(T &&object) noexcept(detail::pfr_to_tuple_noexcept<T>()) {
    using type = std::remove_cvref_t<T>;
    static_assert(IsAggregate<type>, "Type must be an aggregate");

    return detail::pfr_to_tuple(std::forward<T>(object));
}

} // namespace cbor::tags

#include "cbor_tags/cbor_reflection_named.h"

#endif
