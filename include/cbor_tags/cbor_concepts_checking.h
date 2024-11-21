#pragma once

#include "cbor_tags/cbor_concepts.h"

// #include <fmt/core.h>
// #include <magic_enum/magic_enum.hpp>
// #include <nameof.hpp>
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
                                                                            : IsFixedArray<unwrap_type_t<T>>        ? 5
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

} // namespace cbor::tags