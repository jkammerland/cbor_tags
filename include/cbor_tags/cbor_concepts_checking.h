#pragma once

#include "cbor_tags/cbor_concepts.h"

// #include <fmt/core.h>
// #include <magic_enum/magic_enum.hpp>
// #include <nameof.hpp>
#include <algorithm>
#include <numeric>
#include <type_traits>

namespace cbor::tags {

// Major Type  | Meaning           | Content
// ------------|-------------------|-------------------------
// 0           | unsigned integer  | N (integer in [0, uint64_max])
// 1           | negative integer  | -1-N (integer in [-uint64_max, -1])
// 2           | byte string       | N bytes
// 3           | text string       | N bytes (UTF-8 text)
// 4           | array             | N data items (elements)
// 5           | map               | 2N data items (N {key, value} pairs)
// 6           | tag of number N   | 1 data item
// 7           | simple/float      | specific encoding of a simple type

// HOW TO FIX IsNegative, and use IsSigned for signed integers to represent both negative and positive integers?

// Modified ConceptType
template <typename ByteType, typename T>
struct ConceptType : std::integral_constant<ByteType, static_cast<ByteType>(IsUnsigned<unwrap_type_t<T>>            ? 0
                                                                            : IsNegative<unwrap_type_t<T>>          ? 1
                                                                            : IsBinaryString<unwrap_type_t<T>>      ? 2
                                                                            : IsTextString<unwrap_type_t<T>>        ? 3
                                                                            : IsMap<unwrap_type_t<T>>               ? 4
                                                                            : IsArray<unwrap_type_t<T>>             ? 5
                                                                            : IsTag<unwrap_type_t<T>>               ? 6
                                                                            : IsSimple<unwrap_type_t<T>>            ? 7
                                                                            : IsRangeOfCborValues<unwrap_type_t<T>> ? 5
                                                                                                                    : 8)> {};

// Modified get_major
template <typename ByteType, typename T> constexpr auto get_major(const T &&) {
    return ConceptType<ByteType, std::remove_cvref_t<T>>::value;
}

// Modified is_valid_major for variants
template <typename ByteType, typename... T> constexpr bool is_valid_major(ByteType major) {
    // Helper to check if a type matches the major type
    constexpr auto matches_major = []<typename U>(ByteType m) {
        using Type = unwrap_type_t<U>;

        // fmt::print("Type: {}, major: {}\n", nameof::nameof_short_type<Type>(), static_cast<int>(m));

        // Special case for signed types which can be either positive or negative
        if constexpr (IsEnum<U>) {
            using enum_type = std::underlying_type_t<Type>;
            return is_valid_major<ByteType, enum_type>(m);

        } else if constexpr (IsSigned<Type>) {
            return m <= static_cast<ByteType>(major_type::NegativeInteger);
        } else {
            return m == ConceptType<ByteType, Type>::value;
        }
    };

    return (matches_major.template operator()<T>(major) || ...);
}

/**
 * A trick to pass concepts as template args
constexpr auto CheckUnsigned = [](IsUnsignedWithEnum auto) { return 0; };
constexpr auto CheckNegative = [](IsNegative auto) { return 1; };
constexpr auto CheckBinary   = [](IsBinaryString auto) { return 2; };
constexpr auto CheckText     = [](IsTextString auto) { return 3; };
constexpr auto CheckArray    = [](IsArray auto) { return 4; };
constexpr auto CheckMap      = [](IsMap auto) { return 5; };
constexpr auto CheckTag      = [](IsTag auto) { return 6; };
constexpr auto CheckSimple   = [](IsSimple auto) { return 7; };
constexpr auto CheckSigned   = [](IsSignedWithEnum auto) { return 8; };
*/

template <typename T> constexpr void getMatchCount(std::array<int, 9> &result) {
    if constexpr (IsUnsignedOrEnum<T>) {
        result[0]++;
    }
    if constexpr (IsNegative<T>) {
        result[1]++;
    }
    if constexpr (IsBinaryString<T>) {
        result[2]++;
    }
    if constexpr (IsTextString<T>) {
        result[3]++;
    }
    if constexpr (IsArray<T>) {
        result[4]++;
    }
    if constexpr (IsMap<T>) {
        result[5]++;
    }
    if constexpr (IsTag<T>) {
        result[6]++;
    }
    if constexpr (IsSimple<T>) {
        result[7]++;
    }
    if constexpr (IsSignedOrEnum<T>) {
        result[8]++;
    }
}

template <auto Concept, typename... Ts> constexpr size_t count_satisfying() {
    return (requires { Concept.operator()(std::declval<Ts>()); } + ... + 0);
}

template <auto Concept, typename... Ts> constexpr bool satisfies_atmost_one() { return count_satisfying<Concept, Ts...>() <= 1; }

template <typename Variant, auto... Concepts> struct ValidConceptMapping;

template <template <typename...> typename Variant, typename... Ts, auto... Concepts>
struct ValidConceptMapping<Variant<Ts...>, Concepts...> {
    static constexpr auto counts = []() {
        std::array<int, 9> result{};
        (getMatchCount<Ts>(result), ...);
        return result;
    }();

    static constexpr bool types_map_uniquely = std::all_of(counts.begin(), counts.end(), [](int count) { return count <= 1; });
    static constexpr bool all_types_mapped   = std::accumulate(counts.begin(), counts.end(), 0) >= sizeof...(Ts);

    static constexpr bool value = types_map_uniquely && all_types_mapped;
    static constexpr auto array = counts;
};

template <typename Variant, auto... Concepts>
inline constexpr bool valid_concept_mapping_v = ValidConceptMapping<Variant, Concepts...>::value;

template <typename Variant, auto... Concepts>
inline constexpr auto valid_concept_mapping_array_v = ValidConceptMapping<Variant, Concepts...>::array;

} // namespace cbor::tags