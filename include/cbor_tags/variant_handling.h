#pragma once

#include <concepts>
#include <cstddef>

namespace cbor::tags {

// template <auto Concept, typename... Ts> constexpr size_t count_satisfying() {
//     return (requires { Concept.operator()(std::declval<Ts>()); } + ... + 0);
// }

// template <auto Concept, typename... Ts> constexpr bool satisfies_atmost_one() { return count_satisfying<Concept, Ts...>() <= 1; }

// template <typename Variant, auto... Concepts> struct ValidConceptMapping;

// template <template <typename...> typename Variant, typename... Ts, auto... Concepts>
// struct ValidConceptMapping<Variant<Ts...>, Concepts...> {
//     static constexpr bool types_map_uniquely = (satisfies_atmost_one<Concepts, Ts...>() && ...);

//     static constexpr bool all_types_mapped = ((count_satisfying<Concepts, Ts...>() + ...) >= sizeof...(Ts));

//     static constexpr bool value = types_map_uniquely && all_types_mapped;
// };

// template <typename Variant, auto... Concepts>
// inline constexpr bool valid_concept_mapping_v = ValidConceptMapping<Variant, Concepts...>::value;

} // namespace cbor::tags