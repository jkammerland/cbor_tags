#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"

// #include <fmt/core.h>
// #include <magic_enum/magic_enum.hpp>
// #include <nameof.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <type_traits>
#include <vector>

namespace cbor::tags {

// Major Type  | Meaning           | Content
// ------------|-------------------|-------------------------
// 0           | unsigned integer  | N (integer in [0, uint64_max])
// 1           | negative integer  | -1-N (integer in [-uint64_max, -1])
// 2           | byte string       | N bytes
// 3           | text string       | N bytes (UTF-8 text)
// 4           | array             | N data items (elements)
// 5           | map               | 2N data items (N {key, value} pairs)
// 6           | tag of number N   | 1 data item
// 7           | simple/float      | specific encoding of a simple type

// Modified ConceptType
template <typename ByteType, typename T>
struct ConceptType : std::integral_constant<ByteType, static_cast<ByteType>(IsUnsigned<unwrap_type_t<T>>            ? 0
                                                                            : IsNegative<unwrap_type_t<T>>          ? 1
                                                                            : IsBinaryString<unwrap_type_t<T>>      ? 2
                                                                            : IsTextString<unwrap_type_t<T>>        ? 3
                                                                            : IsArray<unwrap_type_t<T>>             ? 4
                                                                            : IsMap<unwrap_type_t<T>>               ? 5
                                                                            : IsTag<unwrap_type_t<T>>               ? 6
                                                                            : IsSimple<unwrap_type_t<T>>            ? 7
                                                                            : IsRangeOfCborValues<unwrap_type_t<T>> ? 5
                                                                                                                    : 8)> {};

// Modified get_major
template <typename ByteType, typename T> constexpr auto get_major(const T &&) {
    return ConceptType<ByteType, std::remove_cvref_t<T>>::value;
}

// Modified is_valid_major for variants
template <typename ByteType, typename... T> constexpr bool is_valid_major(ByteType major) {
    // Helper to check if a type matches the major type
    constexpr auto matches_major = []<typename U>(ByteType m) {
        using Type = unwrap_type_t<U>;

        // fmt::print("Type: {}, major: {}\n", nameof::nameof_short_type<Type>(), static_cast<int>(m));

        // Special case for signed types which can be either positive or negative
        if constexpr (IsEnum<U>) {
            using enum_type = std::underlying_type_t<Type>;
            return is_valid_major<ByteType, enum_type>(m);
        } else if constexpr (IsSigned<Type>) {
            return m <= static_cast<ByteType>(major_type::NegativeInteger);
        } else {
            return m == ConceptType<ByteType, Type>::value;
        }
    };

    return (matches_major.template operator()<T>(major) || ...);
}

/**
 * A trick to pass concepts as template args
constexpr auto CheckUnsigned = [](IsUnsignedWithEnum auto) { return 0; };
constexpr auto CheckNegative = [](IsNegative auto) { return 1; };
constexpr auto CheckBinary   = [](IsBinaryString auto) { return 2; };
constexpr auto CheckText     = [](IsTextString auto) { return 3; };
constexpr auto CheckArray    = [](IsArray auto) { return 4; };
constexpr auto CheckMap      = [](IsMap auto) { return 5; };
constexpr auto CheckTag      = [](IsTag auto) { return 6; };
constexpr auto CheckSimple   = [](IsSimple auto) { return 7; };
constexpr auto CheckSigned   = [](IsSignedWithEnum auto) { return 8; };
*/

namespace detail {
struct MajorIndex {
    static constexpr std::uint64_t Unsigned     = 0;
    static constexpr std::uint64_t Negative     = 1;
    static constexpr std::uint64_t BStr         = 2;
    static constexpr std::uint64_t TStr         = 3;
    static constexpr std::uint64_t Array        = 4;
    static constexpr std::uint64_t Map          = 5;
    static constexpr std::uint64_t Tag          = 6;
    static constexpr std::uint64_t SimpleAny    = 7;
    static constexpr std::uint64_t Unmatched    = 8;
    static constexpr std::uint64_t DynamicTag   = 9;
    static constexpr std::uint64_t AnyTagHeader = 10;
    static constexpr std::uint64_t beginSimples = 11;
    static constexpr std::uint64_t Boolean      = 12;
    static constexpr std::uint64_t float16      = 13;
    static constexpr std::uint64_t float32      = 14;
    static constexpr std::uint64_t float64      = 15;
    static constexpr std::uint64_t Null         = 16;
    static constexpr std::uint64_t SimpleValued = 17;
    static constexpr std::uint64_t endSimples   = 18;
    // static constexpr std::uint64_t ClassWithTag = 19;
};

constexpr std::uint64_t MaxBucketsForVariantChecking = 20;
constexpr std::uint64_t MaxTagsForVariantChecking    = 2048;

} // namespace detail

template <typename Variant, auto... Concepts> struct ValidConceptMapping;

template <template <typename...> typename Variant, typename... Ts, auto... Concepts>
struct ValidConceptMapping<Variant<Ts...>, Concepts...> {

    static constexpr auto counts_fn_inner = [](std::array<uint64_t, detail::MaxBucketsForVariantChecking> &result,
                                               std::vector<uint64_t>                                      &tags,
                                               std::vector<SimpleType> &simples) { (getMatchCount<Ts>(result, tags, simples), ...); };

    static constexpr auto tags_fn_inner = [](std::array<uint64_t, detail::MaxTagsForVariantChecking> &result, std::vector<uint64_t> &tags,
                                             std::vector<SimpleType> &simples) { (getTagsCounts<Ts>(result, tags, simples), ...); };

    static constexpr auto counts_fn_outer = []() mutable {
        using namespace detail;
        std::array<uint64_t, detail::MaxBucketsForVariantChecking> result{};
        std::vector<uint64_t>                                      tags;
        std::vector<SimpleType>                                    simples;
        counts_fn_inner(result, tags, simples);

        /* FINILIZE THE TAG AND SIMPLE BINS */
        if (tags.size() > 0 || result[MajorIndex::AnyTagHeader] > 0) {
            result[MajorIndex::Tag]++; // If any tag has been duplicated, this will be > 1, i.e invalid
        }
        if (simples.size() > 0) {
            result[MajorIndex::SimpleAny]    = 1;
            result[MajorIndex::Boolean]      = std::count(simples.begin(), simples.end(), SimpleType::Bool_False);
            result[MajorIndex::float16]      = std::count(simples.begin(), simples.end(), SimpleType::Float16);
            result[MajorIndex::float32]      = std::count(simples.begin(), simples.end(), SimpleType::Float32);
            result[MajorIndex::float64]      = std::count(simples.begin(), simples.end(), SimpleType::Float64);
            result[MajorIndex::Null]         = std::count(simples.begin(), simples.end(), SimpleType::Null);
            result[MajorIndex::SimpleValued] = std::count(simples.begin(), simples.end(), SimpleType::Simple);
        }
        return result;
    };

    static constexpr auto tags_fn_outer = []() mutable {
        using namespace detail;
        std::array<uint64_t, detail::MaxTagsForVariantChecking> result{};
        std::vector<uint64_t>                                   tags;
        std::vector<SimpleType>                                 simples;
        tags_fn_inner(result, tags, simples);

        for (uint64_t i = 0; i < tags.size(); i++) {
            result[i] = tags[i];
        }

        return result;
    };

    static constexpr auto tags_size_outer = []() {
        using namespace detail;
        std::array<uint64_t, detail::MaxTagsForVariantChecking> result{};
        std::vector<uint64_t>                                   tags;
        std::vector<SimpleType>                                 simples;
        tags_fn_inner(result, tags, simples);
        return tags.size();
    };

    static constexpr auto majors_and_subtypes_counts = counts_fn_outer();
    static constexpr auto tags                       = tags_fn_outer();

    static constexpr uint64_t number_of_tags = (tags_size_outer());
    static constexpr bool     too_many_tags  = (number_of_tags >= detail::MaxTagsForVariantChecking); // Use a runtime map instead

    static constexpr bool no_dynamic_tags    = (majors_and_subtypes_counts[detail::MajorIndex::DynamicTag] == 0);
    static constexpr bool has_any_tag_header = (majors_and_subtypes_counts[detail::MajorIndex::AnyTagHeader] > 0);
    static constexpr bool types_map_uniquely =
        std::all_of(majors_and_subtypes_counts.begin(), majors_and_subtypes_counts.end(), [](auto count) { return count <= 1; });

    static constexpr auto number_of_unmatched = majors_and_subtypes_counts[detail::MajorIndex::Unmatched];
    static constexpr bool value               = types_map_uniquely && no_dynamic_tags && !too_many_tags;
    static constexpr auto array               = majors_and_subtypes_counts;
};

template <typename Variant, auto... Concepts>
inline constexpr bool valid_concept_mapping_v = ValidConceptMapping<Variant, Concepts...>::value;

template <typename Variant, auto... Concepts>
inline constexpr auto valid_concept_mapping_array_v = ValidConceptMapping<Variant, Concepts...>::array;

template <typename Variant, auto... Concepts>
inline constexpr auto valid_concept_mapping_n_unmatched_v = ValidConceptMapping<Variant, Concepts...>::number_of_unmatched;

template <typename T>
constexpr void getMatchCount(std::array<uint64_t, detail::MaxBucketsForVariantChecking> &result, std::vector<uint64_t> &tags,
                             std::vector<SimpleType> &simples) {
    using namespace detail;
    bool unmatched = true;

    /* SPECIAL CASES */
    if constexpr (is_dynamic_tag_t<T>) {
        unmatched = false;
        result[MajorIndex::DynamicTag]++; // Not ok to have dynamic tags
        return;
    }

    if constexpr (IsOptional<T>) {
        unmatched        = false;
        auto current_tag = get_simple_tag_of_primitive_type<std::nullptr_t>();
        simples.push_back(current_tag);
        getMatchCount<typename T::value_type>(result, tags, simples);
        return;
    }
    if constexpr (IsVariant<T>) {
        unmatched = false;
        ValidConceptMapping<T>::counts_fn_inner(result, tags, simples);
        return;
    }
    // if constexpr (IsExpected<T>) { /* ... */}
    /* ----------- */

    if constexpr (IsUnsignedOrEnum<T> || IsSignedOrEnum<T>) {
        unmatched = false;
        result[MajorIndex::Unsigned]++;
    }
    if constexpr (IsNegative<T> || IsSignedOrEnum<T>) {
        unmatched = false;
        result[MajorIndex::Negative]++; // Helper to check if type exists in options
    }
    if constexpr (IsBinaryString<T>) {
        unmatched = false;
        result[MajorIndex::BStr]++;
    }
    if constexpr (IsTextString<T>) {
        unmatched = false;
        result[MajorIndex::TStr]++;
    }
    if constexpr (IsArray<T>) {
        unmatched = false;
        result[MajorIndex::Array]++;
    }
    if constexpr (IsMap<T>) {
        unmatched = false;
        result[MajorIndex::Map]++;
    }
    if constexpr (IsTag<T>) {
        unmatched = false;

        if constexpr (HasInlineTag<T>) {
            auto it = std::find(tags.begin(), tags.end(), T::cbor_tag);
            if (it == tags.end()) {
                tags.push_back(T::cbor_tag);
            } else {
                result[MajorIndex::Tag]++; // If duplicate tag is found
            }
        } else if constexpr (HasStaticTag<T>) {
            auto it = std::find(tags.begin(), tags.end(), decltype(T::cbor_tag){});
            if (it == tags.end()) {
                tags.push_back(decltype(T::cbor_tag){});
            } else {
                result[MajorIndex::Tag]++; // If duplicate tag is found
            }
        } else if constexpr (IsTaggedTuple<T>) {
            auto it = std::find(tags.begin(), tags.end(), std::get<0>(T{}).cbor_tag);
            if (it == tags.end()) {
                tags.push_back(std::get<0>(T{}).cbor_tag);
            } else {
                result[MajorIndex::Tag]++; // If duplicate tag is found
            }
        } else if constexpr (IsTagHeader<T>) {
            result[MajorIndex::AnyTagHeader]++;
        } else {
            result[MajorIndex::Tag]++;
        }
    }
    if constexpr (IsSimple<T>) {
        unmatched = false;

        auto current_tag = get_simple_tag_of_primitive_type<T>();
        if (current_tag == SimpleType::Undefined) {
            result[detail::MajorIndex::Unmatched]++;
            return;
        }

        simples.push_back(current_tag);
    }

    if (unmatched) {
        result[MajorIndex::Unmatched]++;
    }
}

template <typename T>
constexpr void getTagsCounts(std::array<uint64_t, detail::MaxTagsForVariantChecking> &result, std::vector<uint64_t> &tags,
                             std::vector<SimpleType> &simples) {
    using namespace detail;

    if constexpr (IsOptional<T>) {
        auto current_tag = get_simple_tag_of_primitive_type<std::nullptr_t>();
        simples.push_back(current_tag);
        getTagsCounts<typename T::value_type>(result, tags, simples);
        return;
    }
    if constexpr (IsVariant<T>) {
        ValidConceptMapping<T>::tags_fn_inner(result, tags, simples);
        return;
    }

    if constexpr (IsTag<T>) {
        if constexpr (HasInlineTag<T>) {
            auto it = std::find(tags.begin(), tags.end(), T::cbor_tag);
            if (it == tags.end()) {
                tags.push_back(T::cbor_tag);
            } else {
                result[MajorIndex::Tag]++; // If duplicate tag is found
            }
        } else if constexpr (HasStaticTag<T>) {
            auto it = std::find(tags.begin(), tags.end(), decltype(T::cbor_tag){});
            if (it == tags.end()) {
                tags.push_back(decltype(T::cbor_tag){});
            } else {
                result[MajorIndex::Tag]++; // If duplicate tag is found
            }
        } else if constexpr (IsTaggedTuple<T>) {
            auto it = std::find(tags.begin(), tags.end(), std::get<0>(T{}).cbor_tag);
            if (it == tags.end()) {
                tags.push_back(std::get<0>(T{}).cbor_tag);
            } else {
                result[MajorIndex::Tag]++; // If duplicate tag is found
            }
        } else if constexpr (IsTagHeader<T>) {
            result[MajorIndex::AnyTagHeader]++;
        } else {
            result[MajorIndex::Tag]++;
        }
    }
}

} // namespace cbor::tags