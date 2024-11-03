#pragma once

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/float16_ieee754.h"

#include <cmath>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ranges>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace cbor::tags {

template <typename Compare> struct cbor_variant_visitor {
    template <typename T, typename U> constexpr bool operator()(const T &lhs, const U &rhs) const noexcept {
        using namespace cbor::tags;

        if constexpr (std::is_same_v<T, U>) {
            if constexpr (IsUnsigned<T> || IsSigned<T>) {
                return Compare{}(lhs, rhs);
            } else if constexpr (IsBinaryString<T> || IsTextString<T> || IsArray<T> || IsMap<T>) {
                if (lhs.size() != rhs.size()) {
                    return Compare{}(lhs.size(), rhs.size());
                }
                auto cmp = std::lexicographical_compare_three_way(std::ranges::begin(lhs), std::ranges::end(lhs), std::ranges::begin(rhs),
                                                                  std::ranges::end(rhs));
                if constexpr (std::is_same_v<Compare, std::less<>>) {
                    return cmp < 0;
                } else {
                    return cmp > 0;
                }

            } else if constexpr (IsTag<T>) {
                // Assuming Tag types have their own comparison operators
                return Compare{}(lhs, rhs);
            } else if constexpr (IsSimple<T>) {
                // Handle simple types (null, undefined, bool, etc.)
                if constexpr (std::is_same_v<T, std::nullptr_t>) {
                    return false; // nullptr == nullptr
                } else {
                    return Compare{}(lhs, rhs);
                }
            } else {
                static_assert(always_false<T>::value, "Non-exhaustive visitor!");
            }
        } else {
            // Different types
            return false;
        }
    }
};

template <typename Compare = std::less<>> struct variant_comparator {
    template <typename Variant> bool operator()(const Variant &lhs, const Variant &rhs) const {
        if (lhs.index() != rhs.index()) {
            return Compare{}(lhs.index(), rhs.index());
        }

        return std::visit(cbor_variant_visitor<Compare>{}, lhs, rhs);
    }
};

enum class major_type : std::uint8_t {
    UnsignedInteger = 0,
    NegativeInteger = 1,
    ByteString      = 2,
    TextString      = 3,
    Array           = 4,
    Map             = 5,
    Tag             = 6,
    SimpleOrFloat   = 7
};

// [](const auto &l, const auto &r) -> std::strong_ordering {
//             using L = std::decay_t<decltype(l)>;
//             using R = std::decay_t<decltype(r)>;

//             if constexpr (std::is_same_v<L, R>) {
//                 if constexpr (std::is_same_v<L, std::nullptr_t>) {
//                     return std::strong_ordering::equal;
//                 } else if constexpr (std::is_same_v<L, std::span<const std::byte>>) {
//                     return lexicographic_compare(l, r);
//                 } else if constexpr (std::is_same_v<L, std::string_view>) {
//                     return lexicographic_compare(l, r);
//                 } else if constexpr (std::is_same_v<L, binary_array_view> || std::is_same_v<L, binary_map_view> ||
//                                      std::is_same_v<L, binary_tag_view>) {
//                     return l <=> r;
//                 } else if constexpr (std::is_same_v<L, bool>) {
//                     return l <=> r;
//                 } else if constexpr (std::is_same_v<L, float> || std::is_same_v<L, double>) {
//                     if (l < r) {
//                         return std::strong_ordering::less;
//                     }
//                     if (l > r) {
//                         return std::strong_ordering::greater;
//                     }
//                     return std::strong_ordering::equal;
//                 } else if constexpr (std::is_arithmetic_v<L>) {
//                     return l <=> r;
//                 } else {
//                     // This should never happen
//                     // std::unreachable(); // C++23
//                     return std::strong_ordering::equal;
//                 }
//             } else {
//                 // This should never happen due to the index check
//                 // std::unreachable(); // C++23
//                 return std::strong_ordering::equal;
//             }
//         }

} // namespace cbor::tags
