#pragma once

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_integer.h"
#include "cbor_tags/cbor_simple.h"
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

struct variant_hasher {
    template <IsVariant Variant> size_t operator()(const Variant &v) const noexcept {
        // Start with the index hash
        size_t seed = std::hash<size_t>{}(v.index());

        // Combine with value hash
        return std::visit(
            [seed](const auto &arg) mutable -> size_t {
                using T = std::decay_t<decltype(arg)>;

                size_t value_hash;
                if constexpr (IsUnsigned<T> || IsSigned<T>) {
                    value_hash = std::hash<T>{}(arg);
                } else if constexpr (IsBinaryString<T> || IsTextString<T>) {
                    // Hash the contents of the string/binary data
                    value_hash =
                        std::hash<std::string_view>{}(std::string_view(reinterpret_cast<const char *>(std::data(arg)), std::size(arg)));
                } else if constexpr (IsArray<T> || IsMap<T>) {
                    // Hash each element in the container
                    value_hash = 0;
                    for (const auto &elem : arg) {
                        // Combine hashes using FNV-1a-like algorithm
                        value_hash ^= std::hash<std::decay_t<decltype(elem)>>{}(elem);
                        value_hash *= 16777619;
                    }
                } else if constexpr (IsTag<T>) {
                    value_hash = std::hash<T>{}(arg);
                } else if constexpr (IsSimple<T>) {
                    if constexpr (std::is_same_v<T, std::nullptr_t>) {
                        value_hash = 0;
                    } else {
                        value_hash = std::hash<T>{}(arg);
                    }
                } else {
                    static_assert(always_false<T>::value, "Non-exhaustive visitor!");
                }

                // Combine the seed (index hash) with the value hash
                // Using FNV-1a-like combining
                seed ^= value_hash;
                seed *= 16777619;
                return seed;
            },
            v);
    }
};

} // namespace cbor::tags
