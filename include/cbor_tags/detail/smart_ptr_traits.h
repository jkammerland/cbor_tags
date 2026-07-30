#pragma once

#include "cbor_tags/cbor_concepts_checking.h"
#include "cbor_tags/cbor_reflection.h"
#include "cbor_tags/detail/cbor_pointer_traits.h"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace cbor::tags::ext::smart_ptr {

template <typename Pointer> struct pointee_types_for {
    using pointer_type = std::remove_cvref_t<Pointer>;
};

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

template <typename Pointer>
concept HasRegisteredPointeeTypes =
    requires { typename decltype(cbor_smart_pointer_pointee_types(pointee_types_for<std::remove_cvref_t<Pointer>>{}))::type; };

template <typename Pointer>
using registered_pointee_types_t =
    typename decltype(cbor_smart_pointer_pointee_types(pointee_types_for<std::remove_cvref_t<Pointer>>{}))::type;

template <typename T> consteval bool has_fixed_registered_tag() {
    using type = std::remove_cvref_t<T>;
    return IsTag<type> && !IsTagHeader<type> && !is_dynamic_tag_t<type> && !HasDynamicTag<type> &&
           !cbor::tags::detail::is_dynamic_tagged_tuple_v<type>;
}

template <typename T> consteval std::uint64_t registered_pointee_tag() {
    return static_cast<std::uint64_t>(cbor::tags::detail::get_tag_from_any<std::remove_cvref_t<T>>());
}

template <typename Tuple, std::size_t I = 0U, std::size_t J = 1U> consteval bool registered_pointee_tags_are_unique() {
    constexpr auto count = std::tuple_size_v<std::remove_cvref_t<Tuple>>;
    if constexpr (I >= count) {
        return true;
    } else if constexpr (J >= count) {
        return registered_pointee_tags_are_unique<Tuple, I + 1U, I + 2U>();
    } else {
        using left  = std::tuple_element_t<I, std::remove_cvref_t<Tuple>>;
        using right = std::tuple_element_t<J, std::remove_cvref_t<Tuple>>;
        return registered_pointee_tag<left>() != registered_pointee_tag<right>() && registered_pointee_tags_are_unique<Tuple, I, J + 1U>();
    }
}

template <typename Pointer, std::size_t... Is> consteval bool registered_pointee_types_are_valid(std::index_sequence<Is...>) {
    using pointer_type = std::remove_cvref_t<Pointer>;
    using element_type = pointer_element_t<pointer_type>;
    using tuple_type   = registered_pointee_types_t<pointer_type>;

    return sizeof...(Is) != 0U && std::is_polymorphic_v<element_type> &&
           (!UniquePointer<pointer_type> || std::has_virtual_destructor_v<element_type>) &&
           (std::derived_from<std::tuple_element_t<Is, tuple_type>, element_type> && ...) &&
           ((!std::same_as<std::tuple_element_t<Is, tuple_type>, element_type>) && ...) &&
           (std::default_initializable<std::tuple_element_t<Is, tuple_type>> && ...) &&
           (has_fixed_registered_tag<std::tuple_element_t<Is, tuple_type>>() && ...) &&
           (requires(pointer_type &pointer, std::tuple_element_t<Is, tuple_type> *raw) { pointer.reset(raw); } && ...) &&
           registered_pointee_tags_are_unique<tuple_type>();
}

template <typename Pointer> consteval bool registered_pointee_types_are_valid() {
    if constexpr (!HasRegisteredPointeeTypes<Pointer>) {
        return false;
    } else {
        using tuple_type = registered_pointee_types_t<Pointer>;
        if constexpr (!requires { typename std::tuple_size<std::remove_cvref_t<tuple_type>>::type; }) {
            return false;
        } else {
            return registered_pointee_types_are_valid<Pointer>(
                std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<tuple_type>>>{});
        }
    }
}

template <typename Options, typename T> consteval bool encodes_one_cbor_item();

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
    return (encodes_one_cbor_item<Options, std::tuple_element_t<Offset + Is, tuple_type>>() && ...);
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

template <typename Options, typename T> consteval bool encodes_one_cbor_item() {
    using type = std::remove_cvref_t<T>;

    if constexpr (SmartPointer<type>) {
        return true;
    }
    if constexpr (IsAnyHeader<type> || IsTagOnlyTuple<type> || is_static_tag_t<type>::value || is_dynamic_tag_t<type>) {
        return false;
    }
    if constexpr (IsOptional<type>) {
        return encodes_one_cbor_item<Options, typename type::value_type>();
    }
    if constexpr (IsVariant<type>) {
        return cbor::tags::detail::with_variant_alternatives<type>(
            []<typename... Ts>() { return (encodes_one_cbor_item<Options, Ts>() && ...); });
    }
    if constexpr (IsMap<type> && requires {
                      typename type::key_type;
                      typename type::mapped_type;
                  }) {
        return encodes_one_cbor_item<Options, typename type::key_type>() && encodes_one_cbor_item<Options, typename type::mapped_type>();
    }
    if constexpr (IsArray<type> && requires { typename type::value_type; }) {
        return encodes_one_cbor_item<Options, typename type::value_type>();
    }
    if constexpr (IsTaggedTuple<type>) {
        return tuple_payload_encodes_one_item<Options, type, 1U>();
    }
    if constexpr (IsClassWithTagOverload<type>) {
        return true;
    }
    if constexpr (IsAggregate<type> || IsUntaggedTuple<type>) {
        using tuple_type = pointer_tuple_t<type>;
        if constexpr (IsTag<type> && !HasInlineTag<type>) {
            return tuple_payload_encodes_one_item<Options, tuple_type, 1U>();
        } else {
            return tuple_payload_encodes_one_item<Options, tuple_type>();
        }
    }
    if constexpr (IsCborMajor<type>) {
        return true;
    }
    return false;
}

enum class pointer_variant_support : std::uint8_t { supported, unsupported };

struct pointer_variant_profile {
    std::array<bool, cbor::tags::detail::MaxBucketsForVariantChecking>       buckets{};
    std::array<std::uint64_t, cbor::tags::detail::MaxTagsForVariantChecking> tags{};
    std::size_t                                                              tag_count{};
    pointer_variant_support                                                  support{pointer_variant_support::supported};
    bool                                                                     any_tag{};
    bool                                                                     dynamic_tag{};
    bool                                                                     contains_unique_pointer{};
    bool                                                                     contains_shared_pointer{};
    bool                                                                     unique_pointer_null{};
    bool                                                                     shared_pointer_null{};

    constexpr void add_tag(std::uint64_t tag) {
        for (std::size_t index = 0; index < tag_count; ++index) {
            if (tags[index] == tag) {
                return;
            }
        }
        if (tag_count == tags.size()) {
            support = pointer_variant_support::unsupported;
            return;
        }
        tags[tag_count++] = tag;
    }

    constexpr void merge(const pointer_variant_profile &other) {
        for (std::size_t index = 0; index < buckets.size(); ++index) {
            buckets[index] = buckets[index] || other.buckets[index];
        }
        for (std::size_t index = 0; index < other.tag_count; ++index) {
            add_tag(other.tags[index]);
        }
        if (other.support == pointer_variant_support::unsupported) {
            support = pointer_variant_support::unsupported;
        }
        any_tag                 = any_tag || other.any_tag;
        dynamic_tag             = dynamic_tag || other.dynamic_tag;
        contains_unique_pointer = contains_unique_pointer || other.contains_unique_pointer;
        contains_shared_pointer = contains_shared_pointer || other.contains_shared_pointer;
        unique_pointer_null     = unique_pointer_null || other.unique_pointer_null;
        shared_pointer_null     = shared_pointer_null || other.shared_pointer_null;
    }
};

template <typename... Ts> struct pointer_profile_seen_types {};

template <typename T, typename Seen> struct pointer_profile_seen_contains;

template <typename T, typename... Seen>
struct pointer_profile_seen_contains<T, pointer_profile_seen_types<Seen...>>
    : std::bool_constant<(std::same_as<std::remove_cvref_t<T>, Seen> || ...)> {};

template <typename Seen, typename T> struct pointer_profile_seen_append;

template <typename... Seen, typename T> struct pointer_profile_seen_append<pointer_profile_seen_types<Seen...>, T> {
    using type = pointer_profile_seen_types<Seen..., std::remove_cvref_t<T>>;
};

template <typename Seen, typename T> using pointer_profile_seen_append_t = typename pointer_profile_seen_append<Seen, T>::type;

template <typename Options, typename T, typename Seen = pointer_profile_seen_types<>>
consteval pointer_variant_profile collect_pointer_variant_profile();

template <typename Options, typename Tuple, typename Seen, std::size_t... Is>
consteval pointer_variant_profile collect_registered_pointee_profile(std::index_sequence<Is...>) {
    pointer_variant_profile result;
    (result.merge(collect_pointer_variant_profile<Options, std::tuple_element_t<Is, std::remove_cvref_t<Tuple>>, Seen>()), ...);
    return result;
}

template <typename Options, typename Pointer, typename Seen> consteval pointer_variant_profile collect_registered_pointee_profile() {
    using tuple_type = registered_pointee_types_t<std::remove_cvref_t<Pointer>>;
    return collect_registered_pointee_profile<Options, tuple_type, Seen>(
        std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<tuple_type>>>{});
}

template <typename Options, typename Tuple, std::size_t Offset, typename Seen, std::size_t... Is>
consteval pointer_variant_profile collect_tuple_payload_profile(std::index_sequence<Is...>) {
    pointer_variant_profile result;
    (result.merge(collect_pointer_variant_profile<Options, std::tuple_element_t<Offset + Is, std::remove_cvref_t<Tuple>>, Seen>()), ...);
    return result;
}

template <typename Options, typename Tuple, std::size_t Offset, typename Seen>
consteval pointer_variant_profile collect_tuple_payload_profile() {
    using tuple_type     = std::remove_cvref_t<Tuple>;
    constexpr auto total = std::tuple_size_v<tuple_type>;
    if constexpr (total <= Offset) {
        pointer_variant_profile result;
        result.support = pointer_variant_support::unsupported;
        return result;
    } else {
        return collect_tuple_payload_profile<Options, tuple_type, Offset, Seen>(std::make_index_sequence<total - Offset>{});
    }
}

template <typename T> consteval pointer_variant_profile collect_core_pointer_variant_profile() {
    using type             = std::remove_cvref_t<T>;
    using one_type_variant = std::variant<type>;

    constexpr auto mapping    = valid_concept_mapping_array_v<one_type_variant>;
    constexpr auto fixed_tags = ValidConceptMapping<one_type_variant>::tags;
    constexpr auto tags_count = ValidConceptMapping<one_type_variant>::number_of_tags;

    pointer_variant_profile result;
    for (std::size_t index = 0; index < mapping.size(); ++index) {
        result.buckets[index] = mapping[index] != 0U;
    }
    for (std::size_t index = 0; index < tags_count; ++index) {
        result.add_tag(fixed_tags[index]);
    }
    result.any_tag     = mapping[cbor::tags::detail::MajorIndex::AnyTagHeader] != 0U;
    result.dynamic_tag = mapping[cbor::tags::detail::MajorIndex::DynamicTag] != 0U;
    if constexpr (!IsCborMajor<type>) {
        result.support = pointer_variant_support::unsupported;
    }
    if (mapping[cbor::tags::detail::MajorIndex::Unmatched] != 0U || result.dynamic_tag) {
        result.support = pointer_variant_support::unsupported;
    }
    return result;
}

template <typename Options, typename T, typename Seen> consteval pointer_variant_profile collect_pointer_variant_profile() {
    using type = std::remove_cvref_t<T>;
    if constexpr (pointer_profile_seen_contains<type, Seen>::value) {
        return {};
    } else {
        using next_seen = pointer_profile_seen_append_t<Seen, type>;

        if constexpr (UniquePointer<type>) {
            auto result = [&] {
                if constexpr (HasRegisteredPointeeTypes<type>) {
                    return collect_registered_pointee_profile<Options, type, next_seen>();
                } else {
                    return collect_pointer_variant_profile<Options, pointer_element_t<type>, next_seen>();
                }
            }();
            result.buckets[cbor::tags::detail::MajorIndex::Null] = true;
            result.contains_unique_pointer                       = true;
            result.unique_pointer_null                           = true;
            if constexpr (!std::default_initializable<type> ||
                          (!HasRegisteredPointeeTypes<type> && !std::default_initializable<pointer_element_t<type>>) ||
                          known_null_wire_v<pointer_element_t<type>>) {
                result.support = pointer_variant_support::unsupported;
            }
            if constexpr (HasRegisteredPointeeTypes<type> && !registered_pointee_types_are_valid<type>()) {
                result.support = pointer_variant_support::unsupported;
            }
            return result;
        } else if constexpr (SharedPointer<type>) {
            pointer_variant_profile result;
            result.buckets[cbor::tags::detail::MajorIndex::Null] = true;
            result.add_tag(shareable_tag);
            result.add_tag(sharedref_tag);
            result.contains_shared_pointer = true;
            result.shared_pointer_null     = true;
            if constexpr (!std::default_initializable<type> ||
                          (!HasRegisteredPointeeTypes<type> && !std::default_initializable<pointer_element_t<type>>)) {
                result.support = pointer_variant_support::unsupported;
            }
            if constexpr (HasRegisteredPointeeTypes<type> && !registered_pointee_types_are_valid<type>()) {
                result.support = pointer_variant_support::unsupported;
            }
            return result;
        } else if constexpr (IsOptional<type>) {
            auto result = collect_pointer_variant_profile<Options, typename type::value_type, next_seen>();
            result.buckets[cbor::tags::detail::MajorIndex::Null] = true;
            if (result.unique_pointer_null || result.shared_pointer_null) {
                result.support = pointer_variant_support::unsupported;
            }
            return result;
        } else if constexpr (IsVariant<type>) {
            pointer_variant_profile result;
            cbor::tags::detail::with_variant_alternatives<type>(
                [&result]<typename... Ts>() { (result.merge(collect_pointer_variant_profile<Options, Ts, next_seen>()), ...); });
            return result;
        } else if constexpr (IsMap<type> && requires {
                                 typename type::key_type;
                                 typename type::mapped_type;
                             }) {
            auto result = collect_core_pointer_variant_profile<type>();
            auto key    = collect_pointer_variant_profile<Options, typename type::key_type, next_seen>();
            auto mapped = collect_pointer_variant_profile<Options, typename type::mapped_type, next_seen>();

            result.contains_unique_pointer = key.contains_unique_pointer || mapped.contains_unique_pointer;
            result.contains_shared_pointer = key.contains_shared_pointer || mapped.contains_shared_pointer;
            if (key.support == pointer_variant_support::unsupported || mapped.support == pointer_variant_support::unsupported) {
                result.support = pointer_variant_support::unsupported;
            } else {
                result.support = pointer_variant_support::supported;
            }
            return result;
        } else if constexpr (IsArray<type> && requires { typename type::value_type; }) {
            auto result  = collect_core_pointer_variant_profile<type>();
            auto element = collect_pointer_variant_profile<Options, typename type::value_type, next_seen>();

            result.contains_unique_pointer = element.contains_unique_pointer;
            result.contains_shared_pointer = element.contains_shared_pointer;
            result.support                 = element.support;
            return result;
        } else if constexpr (IsAggregate<type> || IsUntaggedTuple<type> || IsTaggedTuple<type> || IsTagOnlyTuple<type>) {
            using tuple_type             = pointer_tuple_t<type>;
            constexpr std::size_t offset = IsTag<type> && !HasInlineTag<type> ? 1U : 0U;
            constexpr std::size_t count  = std::tuple_size_v<tuple_type> - offset;

            auto payload = collect_tuple_payload_profile<Options, tuple_type, offset, next_seen>();
            if constexpr (count == 0U) {
                if constexpr (IsTag<type>) {
                    auto result    = collect_core_pointer_variant_profile<type>();
                    result.support = pointer_variant_support::unsupported;
                    return result;
                } else {
                    return payload;
                }
            } else if constexpr (IsTag<type>) {
                auto result                    = collect_core_pointer_variant_profile<type>();
                result.contains_unique_pointer = payload.contains_unique_pointer;
                result.contains_shared_pointer = payload.contains_shared_pointer;
                result.support                 = payload.support;
                if constexpr (count > 1U && !Options::wrap_groups) {
                    result.support = pointer_variant_support::unsupported;
                }
                return result;
            } else if constexpr (count == 1U) {
                return payload;
            } else {
                pointer_variant_profile result;
                result.buckets[cbor::tags::detail::MajorIndex::Array] = true;
                result.contains_unique_pointer                        = payload.contains_unique_pointer;
                result.contains_shared_pointer                        = payload.contains_shared_pointer;
                result.support = Options::wrap_groups ? payload.support : pointer_variant_support::unsupported;
                return result;
            }
        } else {
            return collect_core_pointer_variant_profile<type>();
        }
    }
}

template <typename T, typename Options = default_options>
inline constexpr auto pointer_variant_profile_v = collect_pointer_variant_profile<Options, T>();

template <typename T> inline constexpr bool contains_decodable_unique_pointer_v = pointer_variant_profile_v<T>.contains_unique_pointer;
template <typename T> inline constexpr bool contains_shared_pointer_v           = pointer_variant_profile_v<T>.contains_shared_pointer;
template <typename T> inline constexpr bool contains_decodable_shared_pointer_v = contains_shared_pointer_v<T>;

template <bool AllowUnique, bool AllowShared, typename T>
inline constexpr bool has_pointer_null_wire_v =
    (AllowUnique && pointer_variant_profile_v<T>.unique_pointer_null) || (AllowShared && pointer_variant_profile_v<T>.shared_pointer_null);

[[nodiscard]] constexpr bool pointer_profile_matches_simple(const pointer_variant_profile &profile, std::byte additional_info) {
    using index = cbor::tags::detail::MajorIndex;
    if (additional_info == static_cast<std::byte>(SimpleType::Bool_False) ||
        additional_info == static_cast<std::byte>(SimpleType::Bool_True)) {
        return profile.buckets[index::Boolean];
    }
    if (additional_info == static_cast<std::byte>(SimpleType::Null)) {
        return profile.buckets[index::Null];
    }
    if (additional_info == static_cast<std::byte>(SimpleType::Float16)) {
        return profile.buckets[index::float16];
    }
    if (additional_info == static_cast<std::byte>(SimpleType::Float32)) {
        return profile.buckets[index::float32];
    }
    if (additional_info == static_cast<std::byte>(SimpleType::Float64)) {
        return profile.buckets[index::float64];
    }
    const auto value = std::to_integer<std::uint8_t>(additional_info);
    return profile.buckets[index::SimpleValued] &&
           (value < static_cast<std::uint8_t>(SimpleType::Bool_False) || value == static_cast<std::uint8_t>(SimpleType::Undefined) ||
            value == static_cast<std::uint8_t>(SimpleType::Simple));
}

[[nodiscard]] constexpr bool pointer_profile_matches_tag(const pointer_variant_profile &profile, std::uint64_t tag) {
    if (profile.any_tag) {
        return true;
    }
    for (std::size_t index = 0; index < profile.tag_count; ++index) {
        if (profile.tags[index] == tag) {
            return true;
        }
    }
    return false;
}

template <typename T, typename Options = default_options>
[[nodiscard]] constexpr bool pointer_variant_matches(major_type major, std::byte additional_info, const std::optional<std::uint64_t> &tag) {
    constexpr auto profile = pointer_variant_profile_v<T, Options>;
    using index            = cbor::tags::detail::MajorIndex;

    switch (major) {
    case major_type::UnsignedInteger: return profile.buckets[index::Unsigned];
    case major_type::NegativeInteger: return profile.buckets[index::Negative];
    case major_type::ByteString: return profile.buckets[index::BStr];
    case major_type::TextString: return profile.buckets[index::TStr];
    case major_type::Array: return profile.buckets[index::Array];
    case major_type::Map: return profile.buckets[index::Map];
    case major_type::Tag: return tag.has_value() && pointer_profile_matches_tag(profile, *tag);
    case major_type::Simple: return pointer_profile_matches_simple(profile, additional_info);
    }
    return false;
}

[[nodiscard]] consteval bool pointer_profiles_overlap(const pointer_variant_profile &left, const pointer_variant_profile &right) {
    using index = cbor::tags::detail::MajorIndex;
    constexpr std::array<std::size_t, 12U> comparable_buckets{
        index::Unsigned, index::Negative, index::BStr,    index::TStr,    index::Array, index::Map,
        index::Boolean,  index::float16,  index::float32, index::float64, index::Null,  index::SimpleValued,
    };
    for (const auto bucket : comparable_buckets) {
        if (left.buckets[bucket] && right.buckets[bucket]) {
            return true;
        }
    }

    if ((left.any_tag && (right.any_tag || right.tag_count != 0U)) || (right.any_tag && (left.any_tag || left.tag_count != 0U))) {
        return true;
    }
    for (std::size_t left_index = 0; left_index < left.tag_count; ++left_index) {
        for (std::size_t right_index = 0; right_index < right.tag_count; ++right_index) {
            if (left.tags[left_index] == right.tags[right_index]) {
                return true;
            }
        }
    }
    return false;
}

template <typename A, typename B, typename Options = default_options> consteval bool pointer_wire_shapes_overlap() {
    return pointer_profiles_overlap(pointer_variant_profile_v<A, Options>, pointer_variant_profile_v<B, Options>);
}

template <typename Variant, typename Options = default_options, std::size_t I = 0U, std::size_t J = 1U>
consteval bool pointer_variant_is_unambiguous() {
    constexpr auto count = cbor::tags::detail::variant_size_v<Variant>;
    if constexpr (I >= count) {
        return true;
    } else if constexpr (J >= count) {
        return pointer_variant_is_unambiguous<Variant, Options, I + 1U, I + 2U>();
    } else {
        using left  = cbor::tags::detail::variant_alternative_t<I, Variant>;
        using right = cbor::tags::detail::variant_alternative_t<J, Variant>;
        return !pointer_wire_shapes_overlap<left, right, Options>() && pointer_variant_is_unambiguous<Variant, Options, I, J + 1U>();
    }
}

template <typename Variant, typename Options = default_options> consteval bool pointer_variant_is_supported() {
    return cbor::tags::detail::with_variant_alternatives<Variant>(
        []<typename... Ts>() { return ((pointer_variant_profile_v<Ts, Options>.support == pointer_variant_support::supported) && ...); });
}

} // namespace detail
} // namespace cbor::tags::ext::smart_ptr
