#pragma once

#include "cbor_tags/cbor_concepts_checking.h"
#include "cbor_tags/detail/cbor_pointer_traits.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <variant>

namespace cbor::tags::ext::smart_ptr::detail {

inline constexpr std::uint64_t shareable_tag        = 28U;
inline constexpr std::uint64_t sharedref_tag        = 29U;
inline constexpr std::uint64_t shared_namespace_tag = 296U;

template <typename T>
concept PointerValue = cbor::tags::detail::NullablePointerValue<T>;

template <typename T> struct unique_pointer_traits {
    static constexpr bool decodable = false;
};

template <PointerValue T> struct unique_pointer_traits<std::unique_ptr<T>> {
    using element_type              = T;
    static constexpr bool decodable = std::default_initializable<T>;
};

template <typename T> struct shared_pointer_traits {
    static constexpr bool decodable = false;
};

template <PointerValue T> struct shared_pointer_traits<std::shared_ptr<T>> {
    using element_type              = T;
    static constexpr bool decodable = std::default_initializable<T>;
};

template <typename T> inline constexpr bool decodable_unique_pointer_v = unique_pointer_traits<std::remove_cvref_t<T>>::decodable;

template <typename T> inline constexpr bool decodable_shared_pointer_v = shared_pointer_traits<std::remove_cvref_t<T>>::decodable;

template <typename T> inline constexpr bool decodable_smart_pointer_v = decodable_unique_pointer_v<T> || decodable_shared_pointer_v<T>;

template <typename T> inline constexpr bool known_null_wire_v = cbor::tags::detail::has_known_null_wire_v<T>;

template <typename T> inline constexpr char graph_type_token{};

template <typename T> constexpr const void *graph_type_id() noexcept { return &graph_type_token<std::remove_cvref_t<T>>; }

struct unique_ptr_codec_marker {};
struct shared_ptr_codec_marker {};

template <typename T> inline constexpr bool has_unique_ptr_codec_v = std::is_base_of_v<unique_ptr_codec_marker, std::remove_cvref_t<T>>;

template <typename T> inline constexpr bool has_shared_ptr_codec_v = std::is_base_of_v<shared_ptr_codec_marker, std::remove_cvref_t<T>>;

template <typename T> constexpr bool contains_decodable_unique_pointer();
template <typename T> constexpr bool contains_decodable_shared_pointer();

template <typename T> constexpr bool contains_decodable_unique_pointer() {
    using type = std::remove_cvref_t<T>;
    if constexpr (decodable_unique_pointer_v<type>) {
        return true;
    } else if constexpr (IsOptional<type>) {
        return contains_decodable_unique_pointer<typename type::value_type>();
    } else if constexpr (IsVariant<type>) {
        return cbor::tags::detail::with_variant_alternatives<type>(
            []<typename... Ts>() { return (contains_decodable_unique_pointer<Ts>() || ...); });
    } else if constexpr ((IsArray<type> || IsMap<type>) && requires { typename type::value_type; }) {
        if constexpr (IsMap<type> && requires {
                          typename type::key_type;
                          typename type::mapped_type;
                      }) {
            return contains_decodable_unique_pointer<typename type::key_type>() ||
                   contains_decodable_unique_pointer<typename type::mapped_type>();
        } else {
            return contains_decodable_unique_pointer<typename type::value_type>();
        }
    } else {
        return false;
    }
}

template <typename T> constexpr bool contains_decodable_shared_pointer() {
    using type = std::remove_cvref_t<T>;
    if constexpr (decodable_shared_pointer_v<type>) {
        return true;
    } else if constexpr (IsOptional<type>) {
        return contains_decodable_shared_pointer<typename type::value_type>();
    } else if constexpr (IsVariant<type>) {
        return cbor::tags::detail::with_variant_alternatives<type>(
            []<typename... Ts>() { return (contains_decodable_shared_pointer<Ts>() || ...); });
    } else if constexpr ((IsArray<type> || IsMap<type>) && requires { typename type::value_type; }) {
        if constexpr (IsMap<type> && requires {
                          typename type::key_type;
                          typename type::mapped_type;
                      }) {
            return contains_decodable_shared_pointer<typename type::key_type>() ||
                   contains_decodable_shared_pointer<typename type::mapped_type>();
        } else {
            return contains_decodable_shared_pointer<typename type::value_type>();
        }
    } else {
        return false;
    }
}

template <typename T> inline constexpr bool contains_decodable_unique_pointer_v = contains_decodable_unique_pointer<T>();

template <typename T> inline constexpr bool contains_decodable_shared_pointer_v = contains_decodable_shared_pointer<T>();

template <bool AllowUnique, bool AllowShared, typename T> consteval bool has_pointer_null_wire() {
    using type = std::remove_cvref_t<T>;
    if constexpr (decodable_unique_pointer_v<type>) {
        return AllowUnique;
    } else if constexpr (decodable_shared_pointer_v<type>) {
        return AllowShared;
    } else if constexpr (IsOptional<type>) {
        return has_pointer_null_wire<AllowUnique, AllowShared, typename type::value_type>();
    } else if constexpr (IsVariant<type>) {
        return cbor::tags::detail::with_variant_alternatives<type>(
            []<typename... Ts>() { return (has_pointer_null_wire<AllowUnique, AllowShared, Ts>() || ...); });
    } else {
        return false;
    }
}

template <bool AllowUnique, bool AllowShared, typename T>
inline constexpr bool has_pointer_null_wire_v = has_pointer_null_wire<AllowUnique, AllowShared, T>();

template <typename T> consteval bool wire_matches_null() {
    using type = std::remove_cvref_t<T>;
    if constexpr (decodable_smart_pointer_v<type>) {
        return true;
    } else {
        using one_type_variant = std::variant<type>;
        constexpr auto mapping = valid_concept_mapping_array_v<one_type_variant>;
        return mapping[cbor::tags::detail::MajorIndex::Null] != 0U;
    }
}

template <typename T, std::uint64_t Tag> consteval bool wire_matches_tag() {
    using type = std::remove_cvref_t<T>;
    if constexpr (decodable_unique_pointer_v<type>) {
        return wire_matches_tag<typename unique_pointer_traits<type>::element_type, Tag>();
    } else if constexpr (decodable_shared_pointer_v<type>) {
        return Tag == shareable_tag || Tag == sharedref_tag;
    } else {
        using one_type_variant    = std::variant<type>;
        constexpr auto mapping    = valid_concept_mapping_array_v<one_type_variant>;
        constexpr auto tags       = ValidConceptMapping<one_type_variant>::tags;
        constexpr auto tags_count = ValidConceptMapping<one_type_variant>::number_of_tags;
        if constexpr (mapping[cbor::tags::detail::MajorIndex::AnyTagHeader] != 0U) {
            return true;
        }
        for (std::uint64_t index = 0; index < tags_count; ++index) {
            if (tags[index] == Tag) {
                return true;
            }
        }
        return false;
    }
}

template <typename A, typename B> consteval bool wire_shapes_overlap() {
    using a_type = std::remove_cvref_t<A>;
    using b_type = std::remove_cvref_t<B>;
    if constexpr (decodable_unique_pointer_v<a_type>) {
        using element_type = typename unique_pointer_traits<a_type>::element_type;
        return wire_matches_null<b_type>() || wire_shapes_overlap<element_type, b_type>();
    } else if constexpr (decodable_unique_pointer_v<b_type>) {
        using element_type = typename unique_pointer_traits<b_type>::element_type;
        return wire_matches_null<a_type>() || wire_shapes_overlap<a_type, element_type>();
    } else if constexpr (decodable_shared_pointer_v<a_type>) {
        return wire_matches_null<b_type>() || wire_matches_tag<b_type, shareable_tag>() || wire_matches_tag<b_type, sharedref_tag>();
    } else if constexpr (decodable_shared_pointer_v<b_type>) {
        return wire_matches_null<a_type>() || wire_matches_tag<a_type, shareable_tag>() || wire_matches_tag<a_type, sharedref_tag>();
    } else {
        using pair_variant         = std::variant<a_type, b_type>;
        constexpr auto mapping     = valid_concept_mapping_array_v<pair_variant>;
        constexpr auto unmatched_i = cbor::tags::detail::MajorIndex::Unmatched;
        for (std::size_t index = 0; index < mapping.size(); ++index) {
            if (index != unmatched_i && mapping[index] > 1U) {
                return true;
            }
        }
        return false;
    }
}

template <typename Variant, std::size_t I = 0U, std::size_t J = 1U> consteval bool pointer_variant_is_unambiguous() {
    constexpr auto count = cbor::tags::detail::variant_size_v<Variant>;
    if constexpr (I >= count) {
        return true;
    } else if constexpr (J >= count) {
        return pointer_variant_is_unambiguous<Variant, I + 1U, I + 2U>();
    } else {
        using left  = cbor::tags::detail::variant_alternative_t<I, Variant>;
        using right = cbor::tags::detail::variant_alternative_t<J, Variant>;
        return !wire_shapes_overlap<left, right>() && pointer_variant_is_unambiguous<Variant, I, J + 1U>();
    }
}

} // namespace cbor::tags::ext::smart_ptr::detail
