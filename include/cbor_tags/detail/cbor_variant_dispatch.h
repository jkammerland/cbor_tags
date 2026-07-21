#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_concepts_checking.h"

#include <cstddef>
#include <type_traits>
#include <variant>

namespace cbor::tags::detail {

// Only a structural mismatch may try the next variant alternative. Malformed
// input and resource failures must be returned to the caller unchanged.
[[nodiscard]] constexpr bool is_retriable_variant_mismatch(status_code status) noexcept {
    return status > status_code::begin_no_match_decoding && status < status_code::end_no_match_decoding;
}

template <bool CatchAllPass, typename U> constexpr bool matches_simple_dispatch(std::byte additional_info) {
    using type = std::remove_cvref_t<U>;
    if constexpr (IsOptional<type>) {
        if (additional_info == static_cast<std::byte>(SimpleType::Null)) {
            return true;
        }
        return matches_simple_dispatch<CatchAllPass, typename type::value_type>(additional_info);
    } else if constexpr (IsVariant<type>) {
        return with_variant_alternatives<type>([additional_info]<typename... Ts>() {
            const auto info = additional_info;
            return (matches_simple_dispatch<CatchAllPass, Ts>(info) || ...);
        });
    } else if constexpr (std::is_same_v<type, simple>) {
        const auto value = std::to_integer<std::uint8_t>(additional_info);
        return CatchAllPass && value <= static_cast<std::uint8_t>(SimpleType::Simple);
    } else if constexpr (IsSimple<type>) {
        return !CatchAllPass && compare_simple_value<type>(additional_info);
    } else {
        return false;
    }
}

template <typename U> constexpr bool matches_major_dispatch(major_type major) {
    using type = std::remove_cvref_t<U>;
    if constexpr (IsOptional<type>) {
        return major == major_type::Simple || matches_major_dispatch<typename type::value_type>(major);
    } else if constexpr (IsVariant<type>) {
        return with_variant_alternatives<type>([major]<typename... Ts>() {
            const auto m = major;
            return (matches_major_dispatch<Ts>(m) || ...);
        });
    } else {
        return is_valid_major<major_type, type>(major);
    }
}

template <typename Variant> consteval void require_unambiguous_variant_dispatch_without_array() {
    constexpr auto mapping = valid_concept_mapping_array_v<Variant>;
    static_assert(mapping[MajorIndex::Unsigned] <= 1, "Multiple types match against major type 0 (unsigned integer)");
    static_assert(mapping[MajorIndex::Negative] <= 1, "Multiple types match against major type 1 (negative integer)");
    static_assert(mapping[MajorIndex::BStr] <= 1, "Multiple types match against major type 2 (byte string)");
    static_assert(mapping[MajorIndex::TStr] <= 1, "Multiple types match against major type 3 (text string)");
    static_assert(mapping[MajorIndex::Map] <= 1, "Multiple types match against major type 5 (map)");
    static_assert(mapping[MajorIndex::Tag] <= 1, "Multiple types match against major type 6 (tag)");
    static_assert(mapping[MajorIndex::SimpleValued] <= 1, "Multiple types match against major type 7 (simple)");
    static_assert(mapping[MajorIndex::Boolean] <= 1, "Multiple types match against major type 7 (boolean)");
    static_assert(mapping[MajorIndex::Null] <= 1, "Multiple types match against major type 7 (null)");
    static_assert(mapping[MajorIndex::float16] <= 1, "Multiple types match against major type 7 (float16)");
    static_assert(mapping[MajorIndex::float32] <= 1, "Multiple types match against major type 7 (float32)");
    static_assert(mapping[MajorIndex::float64] <= 1, "Multiple types match against major type 7 (float64)");
    static_assert(mapping[MajorIndex::DynamicTag] == 0,
                  "Variant cannot contain dynamic tags, must be known at compile time, use as_tag_any to catch any tag");
}

template <typename Variant> consteval void require_unambiguous_variant_dispatch() {
    constexpr auto mapping = valid_concept_mapping_array_v<Variant>;
    static_assert(mapping[MajorIndex::Array] <= 1, "Multiple types match against major type 4 (array)");
    require_unambiguous_variant_dispatch_without_array<Variant>();
    static_assert(valid_concept_mapping_v<Variant>,
                  "Variant has ambiguous major types; only one alternative may match each core CBOR dispatch shape.");
}

} // namespace cbor::tags::detail
