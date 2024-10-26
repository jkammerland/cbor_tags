#pragma once

#include "cbor_tags/cbor_concepts.h"

namespace cbor::tags {

// Type trait to unwrap nested types
template <typename T> struct unwrap_type {
    using type = T;
};

// Specialization for std::optional
template <typename T> struct unwrap_type<std::optional<T>> {
    using type = typename unwrap_type<T>::type;
};

// Specialization for std::variant
template <typename... Ts> struct unwrap_type<std::variant<Ts...>> {
    // Get the first type's unwrapped version
    using type = typename unwrap_type<std::tuple_element_t<0, std::tuple<Ts...>>>::type;
};

// Helper alias
template <typename T> using unwrap_type_t = typename unwrap_type<T>::type;

// Major Type  | Meaning           | Content
// ------------|-------------------|-------------------------
// 0           | unsigned integer  | N
// 1           | negative integer  | -1-N
// 2           | byte string       | N bytes
// 3           | text string       | N bytes (UTF-8 text)
// 4           | array             | N data items (elements)
// 5           | map               | 2N data items (key/value pairs)
// 6           | tag of number N   | 1 data item
// 7           | simple/float      | -

// Modified ConceptType
template <typename ByteType, typename T>
struct ConceptType : std::integral_constant<ByteType, static_cast<ByteType>(IsUnsigned<unwrap_type_t<T>>       ? 0
                                                                            : IsSigned<unwrap_type_t<T>>       ? 1
                                                                            : IsBinaryString<unwrap_type_t<T>> ? 2
                                                                            : IsTextString<unwrap_type_t<T>>   ? 3
                                                                            : IsMap<unwrap_type_t<T>>          ? 4
                                                                            : IsArray<unwrap_type_t<T>>        ? 5
                                                                            : IsTagged<unwrap_type_t<T>>       ? 6
                                                                            : IsSimple<unwrap_type_t<T>>       ? 7
                                                                            : IsRange<unwrap_type_t<T>>        ? 5
                                                                                                               : 255)> {};

// Modified get_major
template <typename ByteType, typename T> constexpr auto get_major(const T &&) {
    return ConceptType<ByteType, std::remove_cvref_t<T>>::value;
}

// Modified is_valid_major for variants
template <typename ByteType, typename... T> constexpr bool is_valid_major(ByteType major) {
    return (... || (major == ConceptType<ByteType, unwrap_type_t<T>>::value));
}

} // namespace cbor::tags