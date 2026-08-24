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

// Upper bound on the arity probed when counting an aggregate's members. The generated
// cbor_reflection_impl.h asserts that its MAX_REFLECTION_MEMBERS matches this value.
constexpr std::size_t MAX_AGGREGATE_PROBE_MEMBERS = 24;

template <std::size_t, typename Probe> using probe_type = Probe;

template <typename T, typename Probe, std::size_t... Is>
constexpr bool braces_constructible_with(std::index_sequence<Is...>) {
    return requires { T{std::declval<probe_type<Is, Probe>>()...}; };
}

// Largest arity that brace-initializes T with the given probe, scanned downwards. Scanning
// downwards rather than upwards matters for aggregates whose lower arities are ill-formed, such
// as one holding a reference member that cannot be left uninitialized.
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
        // std::tuple has constructors beyond its element list (the allocator-extended ones), so
        // brace-init probing over-reports. Its size is already known exactly.
        return std::tuple_size_v<T>;
    } else {
        constexpr auto by_value = probe_arity<T, any, MAX_AGGREGATE_PROBE_MEMBERS>();
        if constexpr (by_value != 0) {
            return by_value;
        } else {
            // `any` converts to a prvalue, so it can never initialize a mutable reference member
            // and such an aggregate fails at every arity. Retry with the reference-returning
            // probe. This stays in a discarded `if constexpr` branch so `any_ref` is never
            // instantiated for an aggregate `any` already counted: binding its lvalue to a
            // by-value member instantiates that member's copy constructor, which is a hard error
            // for types such as std::vector<std::unique_ptr<U>>.
            return probe_arity<T, any_ref, MAX_AGGREGATE_PROBE_MEMBERS>();
        }
    }
}

template <typename T>
    requires IsAggregate<T> || IsTuple<T>
constexpr auto aggregate_binding_count = aggregate_arity<std::remove_cvref_t<T>>();

#endif
} // namespace cbor::tags::detail
