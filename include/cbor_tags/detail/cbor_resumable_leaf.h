#pragma once

#include "cbor_tags/cbor_concepts.h"

namespace cbor::tags::detail {

// Only core leaves whose complete codec has a statically bounded wire size may
// use a borrowed window. Customization checks remain specific to each context.
template <typename T>
inline constexpr bool resumable_builtin_leaf =
    (std::integral<T> && (IsUnsigned<T> || IsSigned<T>) && sizeof(T) <= 8) || IsFloat32<T> || IsFloat64<T> || IsBool<T>;

template <typename T> consteval std::size_t resumable_leaf_encode_size() {
    static_assert(resumable_builtin_leaf<T>);
    if constexpr (IsBool<T>)
        return 1;
    else if constexpr (IsFloat32<T>)
        return 5;
    else if constexpr (IsFloat64<T>)
        return 9;
    else
        return 1 + sizeof(T);
}

template <typename T> consteval std::size_t resumable_leaf_decode_size() {
    static_assert(resumable_builtin_leaf<T>);
    // Narrow integers also accept eight-byte arguments; core conversion policy
    // decides whether their values fit. Floats accept only their matching width.
    if constexpr (IsBool<T> || IsFloat32<T> || IsFloat64<T>)
        return resumable_leaf_encode_size<T>();
    else
        return 9;
}

} // namespace cbor::tags::detail
