#pragma once

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_reflection_config.h"

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

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

#else

// Upper bound for the C++20 brace-probing fallback used when neither native reflection nor
// Boost.PFR names is enabled. The generated cbor_reflection_impl.h asserts that its
// MAX_REFLECTION_MEMBERS matches this value.
constexpr std::size_t MAX_AGGREGATE_PROBE_MEMBERS  = 24;
constexpr std::size_t UNDETECTABLE_AGGREGATE_ARITY = MAX_AGGREGATE_PROBE_MEMBERS + 1U;

template <std::size_t, typename Probe> using probe_type = Probe;

template <typename T, typename Probe, std::size_t... Is> constexpr bool braces_constructible_with(std::index_sequence<Is...>) {
    return requires { T{std::declval<probe_type<Is, Probe>>()...}; };
}

template <typename T, typename Probe, std::size_t N> constexpr std::size_t probe_arity() {
    if constexpr (N == 0) {
        return 0;
    } else if constexpr (braces_constructible_with<T, Probe>(std::make_index_sequence<N>{})) {
        return N;
    } else {
        return probe_arity<T, Probe, N - 1>();
    }
}

template <typename T> constexpr std::size_t aggregate_arity() {
    if constexpr (IsTuple<T>) {
        return std::tuple_size_v<T>;
    } else {
        constexpr auto by_value = probe_arity<T, any, MAX_AGGREGATE_PROBE_MEMBERS>();
        if constexpr (by_value != 0) {
            return by_value;
        } else if constexpr (std::is_trivially_copy_constructible_v<T>) {
            return probe_arity<T, any_ref, MAX_AGGREGATE_PROBE_MEMBERS>();
        } else {
            return UNDETECTABLE_AGGREGATE_ARITY;
        }
    }
}

template <typename T>
    requires IsAggregate<T> || IsTuple<T>
constexpr auto aggregate_binding_count = aggregate_arity<std::remove_cvref_t<T>>();

#endif
} // namespace cbor::tags::detail
