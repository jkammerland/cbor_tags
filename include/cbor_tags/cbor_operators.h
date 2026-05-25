#pragma once

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_integer.h"
#include "cbor_tags/cbor_simple.h"
#include "cbor_tags/float16_ieee754.h"

#include <compare>
#include <concepts>
#include <cstddef>
#include <exception>
#include <functional>
#include <iterator>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace cbor::tags {

template <typename Compare> struct cbor_variant_visitor {
    template <typename T, typename U> constexpr bool operator()(const T &lhs, const U &rhs) const noexcept {
        using namespace cbor::tags;

        if constexpr (std::is_same_v<T, U>) {
            if constexpr (IsUnsigned<T> || IsSigned<T> || IsTag<T>) {
                return Compare{}(lhs, rhs);
            } else if constexpr (IsBinaryString<T> || IsTextString<T> || IsArray<T> || IsMap<T>) {
                if (lhs.size() != rhs.size()) {
                    return Compare{}(lhs.size(), rhs.size());
                }

                // Not fully supported yet
                // auto cmp = std::lexicographical_compare_three_way(std::ranges::begin(lhs), std::ranges::end(lhs),
                // std::ranges::begin(rhs), std::ranges::end(rhs));
                auto cmp = [&]() {
                    auto first1 = std::ranges::begin(lhs);
                    auto last1  = std::ranges::end(lhs);
                    auto first2 = std::ranges::begin(rhs);
                    auto last2  = std::ranges::end(rhs);

                    while (first1 != last1 && first2 != last2) {
                        if (*first1 < *first2)
                            return std::weak_ordering::less;
                        if (*first2 < *first1)
                            return std::weak_ordering::greater;
                        ++first1;
                        ++first2;
                    }

                    if (first2 != last2)
                        return std::weak_ordering::less;
                    if (first1 != last1)
                        return std::weak_ordering::greater;
                    return std::weak_ordering::equivalent;
                }();

                if constexpr (std::is_same_v<Compare, std::less<>>) {
                    return cmp < 0;
                } else {
                    return cmp > 0;
                }

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
    template <IsVariant Variant> bool operator()(const Variant &lhs, const Variant &rhs) const {
        const auto lhs_index = detail::variant_index(lhs);
        const auto rhs_index = detail::variant_index(rhs);
        if (lhs_index != rhs_index) {
            return Compare{}(lhs_index, rhs_index);
        }

        return detail::variant_visit(cbor_variant_visitor<Compare>{}, lhs, rhs);
    }
};

struct variant_hasher {
    template <IsVariant Variant> size_t operator()(const Variant &v) const noexcept {
        // Start with the index hash
        size_t seed = std::hash<size_t>{}(detail::variant_index(v));
        seed ^= hash_variant_value(v);
        seed *= fnv_prime;
        return seed;
    }

  private:
    static constexpr size_t fnv_prime = 16777619;

    template <typename T> static size_t hash_cbor_value(const T &arg) noexcept {
        using value_type = std::decay_t<T>;

        if constexpr (IsUnsigned<value_type> || IsSigned<value_type>) {
            return std::hash<value_type>{}(arg);
        } else if constexpr (IsBinaryString<value_type> || IsTextString<value_type>) {
            // Hash the contents of the string/binary data
            return std::hash<std::string_view>{}(std::string_view(reinterpret_cast<const char *>(std::data(arg)), std::size(arg)));
        } else if constexpr (IsArray<value_type> || IsMap<value_type>) {
            // Hash each element in the container
            size_t value_hash = 0;
            for (const auto &elem : arg) {
                // Combine hashes using FNV-1a-like algorithm
                value_hash ^= std::hash<std::decay_t<decltype(elem)>>{}(elem);
                value_hash *= fnv_prime;
            }
            return value_hash;
        } else if constexpr (IsTag<value_type>) {
            return std::hash<value_type>{}(arg);
        } else if constexpr (IsSimple<value_type>) {
            if constexpr (std::is_same_v<value_type, std::nullptr_t>) {
                return 0;
            } else {
                return std::hash<value_type>{}(arg);
            }
        } else {
            static_assert(always_false<value_type>::value, "Non-exhaustive visitor!");
        }
    }

    template <std::size_t I, IsVariant Variant> static size_t hash_variant_value_impl(const Variant &v) noexcept {
        using variant_type = std::remove_cvref_t<Variant>;

        if constexpr (I == detail::variant_size_v<variant_type>) {
            std::terminate();
        } else {
            if (detail::variant_index(v) == I) {
                return hash_cbor_value(detail::variant_get<I>(v));
            }
            return hash_variant_value_impl<I + 1>(v);
        }
    }

    template <IsVariant Variant> static size_t hash_variant_value(const Variant &v) noexcept { return hash_variant_value_impl<0>(v); }
};

} // namespace cbor::tags
