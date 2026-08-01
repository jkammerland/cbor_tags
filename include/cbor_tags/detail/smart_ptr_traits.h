#pragma once

#include "cbor_tags/cbor_concepts_checking.h"
#include "cbor_tags/cbor_reflection.h"
#include "cbor_tags/detail/cbor_pointer_traits.h"

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace cbor::tags::ext::smart_ptr {

// Custom class encoders are opaque to structural wire-shape inspection. Users
// may opt in only when every encoding customization for T emits one complete
// CBOR item.
template <typename T> inline constexpr bool enable_custom_pointee_single_item = false;

namespace detail {

inline constexpr std::uint64_t shareable_tag = 28U;
inline constexpr std::uint64_t sharedref_tag = 29U;

template <typename T>
concept PointerValue = cbor::tags::detail::SmartPointerElement<T>;

template <typename T>
concept UniquePointer = cbor::tags::detail::IsUniquePointer<T>;

template <typename T>
concept SharedPointer = cbor::tags::detail::IsSharedPointer<T>;

template <typename T>
concept SmartPointer = cbor::tags::detail::IsSmartPointer<T>;

template <SmartPointer Pointer> using pointer_element_t = cbor::tags::detail::smart_pointer_element_t<Pointer>;

template <typename T> inline constexpr bool known_null_wire_v = cbor::tags::detail::has_known_null_wire_v<T>;

template <typename T> inline constexpr char graph_type_token{};
template <typename T> constexpr const void *graph_type_id() noexcept { return &graph_type_token<std::remove_cvref_t<T>>; }

struct pointee_shape_probe_encoder {
    using expected_type = expected<void, status_code>;

    template <typename... Ts> constexpr expected_type operator()(Ts &&...) const { return {}; }
};

template <typename Options, typename T, bool CheckCustomization = true> consteval bool encodes_one_cbor_item();

template <typename T, bool = IsAggregate<std::remove_cvref_t<T>>> struct pointer_tuple {
    using type = std::remove_cvref_t<T>;
};

template <typename T> struct pointer_tuple<T, true> {
    using type = std::remove_cvref_t<decltype(to_tuple(std::declval<std::remove_cvref_t<T> &>()))>;
};

template <typename T> using pointer_tuple_t = typename pointer_tuple<T>::type;

template <typename Options, typename Tuple, std::size_t Offset, std::size_t... Is>
consteval bool tuple_members_encode_one_item(std::index_sequence<Is...>) {
    using tuple_type = std::remove_cvref_t<Tuple>;
    return (encodes_one_cbor_item<Options, std::tuple_element_t<Offset + Is, tuple_type>, false>() && ...);
}

template <typename Options, typename Tuple, std::size_t Offset = 0U> consteval bool tuple_payload_encodes_one_item() {
    using tuple_type     = std::remove_cvref_t<Tuple>;
    constexpr auto total = std::tuple_size_v<tuple_type>;
    if constexpr (total <= Offset) {
        return false;
    } else {
        constexpr auto count = total - Offset;
        if constexpr (count > 1U && !Options::wrap_groups) {
            return false;
        } else {
            return tuple_members_encode_one_item<Options, tuple_type, Offset>(std::make_index_sequence<count>{});
        }
    }
}

template <typename Options, typename T, bool CheckCustomization> consteval bool encodes_one_cbor_item() {
    using type = std::remove_cvref_t<T>;

    if constexpr (IsClassWithEncodingOverload<pointee_shape_probe_encoder, type>) {
        if constexpr (CheckCustomization) {
            return enable_custom_pointee_single_item<type>;
        } else {
            // A nested custom value remains one member of its containing
            // aggregate. Only a customization on the pointer's pointee can
            // change the number of top-level items wrapped by tag 28.
            return true;
        }
    }
    if constexpr (SmartPointer<type>) {
        return true;
    }
    if constexpr (IsAnyHeader<type> || IsTagOnlyTuple<type> || is_static_tag_t<type>::value || is_dynamic_tag_t<type>) {
        return false;
    }
    if constexpr (IsOptional<type>) {
        return encodes_one_cbor_item<Options, typename type::value_type, false>();
    }
    if constexpr (IsVariant<type>) {
        return cbor::tags::detail::with_variant_alternatives<type>(
            []<typename... Ts>() { return (encodes_one_cbor_item<Options, Ts, false>() && ...); });
    }
    if constexpr (IsMap<type> && requires {
                      typename type::key_type;
                      typename type::mapped_type;
                  }) {
        return encodes_one_cbor_item<Options, typename type::key_type, false>() &&
               encodes_one_cbor_item<Options, typename type::mapped_type, false>();
    }
    if constexpr (IsArray<type> && requires { typename type::value_type; }) {
        return encodes_one_cbor_item<Options, typename type::value_type, false>();
    }
    if constexpr (IsTaggedTuple<type>) {
        return tuple_payload_encodes_one_item<Options, type, 1U>();
    }
    if constexpr (IsAggregate<type> || IsUntaggedTuple<type>) {
        using tuple_type = pointer_tuple_t<type>;
        if constexpr (IsClassWithTagOverload<type>) {
            constexpr auto total = std::tuple_size_v<std::remove_cvref_t<tuple_type>>;
            if constexpr (total == 0U) {
                return false;
            } else {
                using first_type          = std::remove_cvref_t<std::tuple_element_t<0U, std::remove_cvref_t<tuple_type>>>;
                constexpr bool stored_tag = is_static_tag_t<first_type>::value || is_dynamic_tag_t<first_type>;
                return tuple_payload_encodes_one_item<Options, tuple_type, stored_tag ? 1U : 0U>();
            }
        } else {
            return tuple_payload_encodes_one_item<Options, tuple_type>();
        }
    }
    if constexpr (IsClassWithTagOverload<type>) {
        // Non-aggregate tagged types are emitted by an explicit codec. The tag
        // and that codec's payload form one item; aggregate tag-only and
        // unwrapped multi-item cases were handled above.
        return true;
    }
    if constexpr (IsCborMajor<type>) {
        return true;
    }
    return false;
}

template <typename T> constexpr bool contains_decodable_unique_pointer();
template <typename T> constexpr bool contains_shared_pointer();

template <typename T> constexpr bool contains_decodable_unique_pointer() {
    using type = std::remove_cvref_t<T>;
    if constexpr (UniquePointer<type>) {
        return std::default_initializable<type> && std::default_initializable<pointer_element_t<type>>;
    }
    if constexpr (IsOptional<type>) {
        return contains_decodable_unique_pointer<typename type::value_type>();
    }
    if constexpr (IsVariant<type>) {
        return cbor::tags::detail::with_variant_alternatives<type>(
            []<typename... Ts>() { return (contains_decodable_unique_pointer<Ts>() || ...); });
    }
    if constexpr (IsMap<type> && requires {
                      typename type::key_type;
                      typename type::mapped_type;
                  }) {
        return contains_decodable_unique_pointer<typename type::key_type>() ||
               contains_decodable_unique_pointer<typename type::mapped_type>();
    }
    if constexpr (IsArray<type> && requires { typename type::value_type; }) {
        return contains_decodable_unique_pointer<typename type::value_type>();
    }
    return false;
}

template <typename T> constexpr bool contains_shared_pointer() {
    using type = std::remove_cvref_t<T>;
    if constexpr (SharedPointer<type>) {
        return true;
    }
    if constexpr (IsOptional<type>) {
        return contains_shared_pointer<typename type::value_type>();
    }
    if constexpr (IsVariant<type>) {
        return cbor::tags::detail::with_variant_alternatives<type>([]<typename... Ts>() { return (contains_shared_pointer<Ts>() || ...); });
    }
    if constexpr (IsMap<type> && requires {
                      typename type::key_type;
                      typename type::mapped_type;
                  }) {
        return contains_shared_pointer<typename type::key_type>() || contains_shared_pointer<typename type::mapped_type>();
    }
    if constexpr (IsArray<type> && requires { typename type::value_type; }) {
        return contains_shared_pointer<typename type::value_type>();
    }
    return false;
}

template <typename T> inline constexpr bool contains_decodable_unique_pointer_v = contains_decodable_unique_pointer<T>();
template <typename T> inline constexpr bool contains_shared_pointer_v           = contains_shared_pointer<T>();
template <typename T> inline constexpr bool contains_decodable_shared_pointer_v = contains_shared_pointer<T>();

template <bool AllowUnique, bool AllowShared, typename T> consteval bool has_pointer_null_wire() {
    using type = std::remove_cvref_t<T>;
    if constexpr (UniquePointer<type>) {
        return AllowUnique;
    } else if constexpr (SharedPointer<type>) {
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
    if constexpr (SmartPointer<type>) {
        return true;
    } else {
        using one_type_variant = std::variant<type>;
        constexpr auto mapping = valid_concept_mapping_array_v<one_type_variant>;
        return mapping[cbor::tags::detail::MajorIndex::Null] != 0U;
    }
}

template <typename T, std::uint64_t Tag> consteval bool wire_matches_tag() {
    using type = std::remove_cvref_t<T>;
    if constexpr (UniquePointer<type>) {
        return wire_matches_tag<pointer_element_t<type>, Tag>();
    } else if constexpr (SharedPointer<type>) {
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
    if constexpr (UniquePointer<a_type>) {
        return wire_matches_null<b_type>() || wire_shapes_overlap<pointer_element_t<a_type>, b_type>();
    } else if constexpr (UniquePointer<b_type>) {
        return wire_matches_null<a_type>() || wire_shapes_overlap<a_type, pointer_element_t<b_type>>();
    } else if constexpr (SharedPointer<a_type>) {
        return wire_matches_null<b_type>() || wire_matches_tag<b_type, shareable_tag>() || wire_matches_tag<b_type, sharedref_tag>();
    } else if constexpr (SharedPointer<b_type>) {
        return wire_matches_null<a_type>() || wire_matches_tag<a_type, shareable_tag>() || wire_matches_tag<a_type, sharedref_tag>();
    } else {
        using pair_variant          = std::variant<a_type, b_type>;
        constexpr auto mapping      = valid_concept_mapping_array_v<pair_variant>;
        constexpr auto unmatched_ix = cbor::tags::detail::MajorIndex::Unmatched;
        for (std::size_t index = 0; index < mapping.size(); ++index) {
            if (index != unmatched_ix && mapping[index] > 1U) {
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

} // namespace detail
} // namespace cbor::tags::ext::smart_ptr
