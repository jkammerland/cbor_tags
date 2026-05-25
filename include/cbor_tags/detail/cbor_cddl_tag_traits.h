#pragma once

#include "cbor_tags/cbor_concepts_checking.h"
#include "cbor_tags/detail/cbor_pointer_traits.h"
#include "cbor_tags/extensions/cddl_traits.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>

namespace cbor::tags::detail {

template <typename T>
concept CDDLTaggedByteStringArray = requires {
    { cddl_tagged_bstr_array_traits<std::remove_cvref_t<T>>::tag } -> std::convertible_to<std::uint64_t>;
};

template <typename T>
concept CDDLHomogeneousArray = requires {
    typename cddl_homogeneous_array_traits<std::remove_cvref_t<T>>::array_type;
    { cddl_homogeneous_array_traits<std::remove_cvref_t<T>>::tag } -> std::convertible_to<std::uint64_t>;
};

template <typename T>
concept CDDLMultiDimensionalArray = requires {
    typename cddl_multi_dimensional_array_traits<std::remove_cvref_t<T>>::dimensions_type;
    typename cddl_multi_dimensional_array_traits<std::remove_cvref_t<T>>::array_type;
    { cddl_multi_dimensional_array_traits<std::remove_cvref_t<T>>::tag } -> std::convertible_to<std::uint64_t>;
};

template <typename T> consteval bool cddl_direct_fixed_tag_available() {
    using value_type = std::remove_cvref_t<T>;
    if constexpr (IsTagHeader<value_type> || is_dynamic_tag_t<value_type> || HasDynamicTag<value_type> ||
                  is_dynamic_tagged_tuple_v<value_type>) {
        return false;
    } else {
        return CDDLTaggedByteStringArray<value_type> || CDDLHomogeneousArray<value_type> || CDDLMultiDimensionalArray<value_type> ||
               IsTag<value_type>;
    }
}

template <typename T> consteval std::uint64_t cddl_direct_fixed_tag() {
    using value_type = std::remove_cvref_t<T>;
    if constexpr (CDDLTaggedByteStringArray<value_type>) {
        return cddl_tagged_bstr_array_traits<value_type>::tag;
    } else if constexpr (CDDLHomogeneousArray<value_type>) {
        return cddl_homogeneous_array_traits<value_type>::tag;
    } else if constexpr (CDDLMultiDimensionalArray<value_type>) {
        return cddl_multi_dimensional_array_traits<value_type>::tag;
    } else {
        return static_cast<std::uint64_t>(get_tag_from_any<value_type>());
    }
}

template <typename T> consteval bool cddl_contains_tag_header() {
    using value_type = std::remove_cvref_t<T>;
    if constexpr (IsOptional<value_type>) {
        return cddl_contains_tag_header<typename value_type::value_type>();
    } else if constexpr (IsVariant<value_type>) {
        return with_variant_alternatives<value_type>([]<typename... Ts>() { return (cddl_contains_tag_header<Ts>() || ...); });
    } else {
        return IsTagHeader<value_type>;
    }
}

template <typename T> consteval bool cddl_contains_fixed_tag() {
    using value_type = std::remove_cvref_t<T>;
    if constexpr (IsOptional<value_type>) {
        return cddl_contains_fixed_tag<typename value_type::value_type>();
    } else if constexpr (IsVariant<value_type>) {
        return with_variant_alternatives<value_type>([]<typename... Ts>() { return (cddl_contains_fixed_tag<Ts>() || ...); });
    } else {
        return cddl_direct_fixed_tag_available<value_type>();
    }
}

template <typename T, std::uint64_t Tag> consteval bool cddl_contains_fixed_tag_value() {
    using value_type = std::remove_cvref_t<T>;
    if constexpr (IsOptional<value_type>) {
        return cddl_contains_fixed_tag_value<typename value_type::value_type, Tag>();
    } else if constexpr (IsVariant<value_type>) {
        return with_variant_alternatives<value_type>([]<typename... Ts>() { return (cddl_contains_fixed_tag_value<Ts, Tag>() || ...); });
    } else if constexpr (cddl_direct_fixed_tag_available<value_type>()) {
        return cddl_direct_fixed_tag<value_type>() == Tag;
    } else {
        return false;
    }
}

template <typename T> consteval bool cddl_contains_shared_graph_pointer() {
    using value_type = std::remove_cvref_t<T>;
    if constexpr (IsOptional<value_type>) {
        return cddl_contains_shared_graph_pointer<typename value_type::value_type>();
    } else if constexpr (IsVariant<value_type>) {
        return with_variant_alternatives<value_type>([]<typename... Ts>() { return (cddl_contains_shared_graph_pointer<Ts>() || ...); });
    } else {
        return is_std_shared_ptr<value_type>::value;
    }
}

template <typename T> consteval bool cddl_contains_shared_graph_collision_tag() {
    return cddl_contains_tag_header<T>() || cddl_contains_fixed_tag_value<T, 28U>() || cddl_contains_fixed_tag_value<T, 29U>();
}

template <typename T> consteval bool cddl_is_direct_nullable_pointer_alternative() { return IsNullablePointer<std::remove_cvref_t<T>>; }

template <typename T> consteval bool cddl_is_shared_graph_vector_alternative() {
    return is_std_vector_of_shared_ptr<std::remove_cvref_t<T>>::value;
}

template <typename A, typename B> consteval bool cddl_fixed_tags_overlap() {
    using a_type = std::remove_cvref_t<A>;
    using b_type = std::remove_cvref_t<B>;
    if constexpr (IsOptional<a_type>) {
        return cddl_fixed_tags_overlap<typename a_type::value_type, b_type>();
    } else if constexpr (IsOptional<b_type>) {
        return cddl_fixed_tags_overlap<a_type, typename b_type::value_type>();
    } else if constexpr (IsVariant<a_type>) {
        return with_variant_alternatives<a_type>([]<typename... Ts>() { return (cddl_fixed_tags_overlap<Ts, b_type>() || ...); });
    } else if constexpr (IsVariant<b_type>) {
        return with_variant_alternatives<b_type>([]<typename... Ts>() { return (cddl_fixed_tags_overlap<a_type, Ts>() || ...); });
    } else if constexpr (cddl_direct_fixed_tag_available<a_type>() && cddl_direct_fixed_tag_available<b_type>()) {
        return cddl_direct_fixed_tag<a_type>() == cddl_direct_fixed_tag<b_type>();
    } else {
        return false;
    }
}

template <typename A, typename B> consteval bool cddl_tag_alternatives_overlap() {
    return cddl_fixed_tags_overlap<A, B>() || (cddl_contains_tag_header<A>() && cddl_contains_fixed_tag<B>()) ||
           (cddl_contains_tag_header<B>() && cddl_contains_fixed_tag<A>()) ||
           (cddl_contains_tag_header<A>() && cddl_contains_tag_header<B>());
}

template <cddl_shared_pointer_mode PointerMode, typename A, typename B> consteval bool cddl_scoped_tag_alternatives_overlap() {
    if constexpr (PointerMode == cddl_shared_pointer_mode::shared_graph) {
        return cddl_tag_alternatives_overlap<A, B>() ||
               (cddl_contains_shared_graph_pointer<A>() && cddl_contains_shared_graph_collision_tag<B>()) ||
               (cddl_contains_shared_graph_pointer<B>() && cddl_contains_shared_graph_collision_tag<A>()) ||
               (cddl_contains_shared_graph_pointer<A>() && cddl_contains_shared_graph_pointer<B>());
    } else {
        return cddl_tag_alternatives_overlap<A, B>();
    }
}

template <cddl_shared_pointer_mode PointerMode> consteval bool cddl_scoped_variant_has_tag_overlap() { return false; }

template <cddl_shared_pointer_mode PointerMode, typename T> consteval bool cddl_scoped_variant_has_tag_overlap() { return false; }

template <cddl_shared_pointer_mode PointerMode, typename T, typename U, typename... Rest>
consteval bool cddl_scoped_variant_has_tag_overlap() {
    return cddl_scoped_tag_alternatives_overlap<PointerMode, T, U>() ||
           (cddl_scoped_tag_alternatives_overlap<PointerMode, T, Rest>() || ...) ||
           cddl_scoped_variant_has_tag_overlap<PointerMode, U, Rest...>();
}

} // namespace cbor::tags::detail
