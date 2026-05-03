#pragma once

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_detail.h"

#include <limits>
#include <tuple>
#include <type_traits>
#include <utility>

namespace cbor::tags {

#if CBOR_TAGS_HAS_STD_REFLECTION

namespace detail {
constexpr size_t MAX_REFLECTION_MEMBERS = std::numeric_limits<size_t>::max() - 1;

template <typename T> consteval auto aggregate_member_count() {
    return std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current()).size();
}

template <typename T, std::size_t I> consteval std::meta::info aggregate_member() {
    return std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::current())[I];
}

template <typename T, std::size_t... Is> constexpr auto to_tuple_impl(T &&object, std::index_sequence<Is...>) noexcept {
    using type = std::remove_cvref_t<T>;
    return std::tie(object.[: aggregate_member<type, Is>() :]...);
}
} // namespace detail

template <class T> constexpr auto to_tuple(T &&object) noexcept;

template <class T> constexpr auto to_tuple(T &&object) noexcept {
    using type = std::remove_cvref_t<T>;
    static_assert(IsAggregate<type>, "Type must be an aggregate");
    constexpr auto member_count = detail::aggregate_member_count<type>();
    return detail::to_tuple_impl(std::forward<T>(object), std::make_index_sequence<member_count>{});
}

#else

template <class T> constexpr auto to_tuple(T &&object) noexcept;

#endif

} // namespace cbor::tags

#if !CBOR_TAGS_HAS_STD_REFLECTION
#include "cbor_tags/cbor_reflection_impl.h"
#endif
