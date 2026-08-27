#pragma once

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_reflection_count.h"

#include <tuple>
#include <type_traits>
#include <utility>

#if !CBOR_TAGS_HAS_STD_REFLECTION && !CBOR_TAGS_HAS_BOOST_PFR_NAMES

namespace cbor::tags {

namespace detail {
constexpr size_t MAX_REFLECTION_MEMBERS = 24;
} // namespace detail

template <class T> constexpr auto to_tuple(T &&object) noexcept {
    using type = std::decay_t<T>;
    using probe = std::conditional_t<std::is_trivially_copy_constructible_v<type>, any_ref, any>;
    static_assert(IsAggregate<type>, "Type must be an aggregate");

    if constexpr (IsTuple<type>) {
        return; // unreachable due to IsAggregate
    } else if constexpr (IsBracesContructible<type, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24);
    } else if constexpr (IsBracesContructible<type, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23);
    } else if constexpr (IsBracesContructible<type, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22);
    } else if constexpr (IsBracesContructible<type, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21);
    } else if constexpr (IsBracesContructible<type, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20);
    } else if constexpr (IsBracesContructible<type, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19);
    } else if constexpr (IsBracesContructible<type, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18);
    } else if constexpr (IsBracesContructible<type, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17);
    } else if constexpr (IsBracesContructible<type, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16);
    } else if constexpr (IsBracesContructible<type, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15);
    } else if constexpr (IsBracesContructible<type, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14);
    } else if constexpr (IsBracesContructible<type, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13);
    } else if constexpr (IsBracesContructible<type, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12);
    } else if constexpr (IsBracesContructible<type, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11);
    } else if constexpr (IsBracesContructible<type, probe, probe, probe, probe, probe, probe, probe, probe, probe, probe>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10);
    } else if constexpr (IsBracesContructible<type, probe, probe, probe, probe, probe, probe, probe, probe, probe>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9);
    } else if constexpr (IsBracesContructible<type, probe, probe, probe, probe, probe, probe, probe, probe>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8);
    } else if constexpr (IsBracesContructible<type, probe, probe, probe, probe, probe, probe, probe>) {
        auto &[p1, p2, p3, p4, p5, p6, p7] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7);
    } else if constexpr (IsBracesContructible<type, probe, probe, probe, probe, probe, probe>) {
        auto &[p1, p2, p3, p4, p5, p6] = object;
        return std::tie(p1, p2, p3, p4, p5, p6);
    } else if constexpr (IsBracesContructible<type, probe, probe, probe, probe, probe>) {
        auto &[p1, p2, p3, p4, p5] = object;
        return std::tie(p1, p2, p3, p4, p5);
    } else if constexpr (IsBracesContructible<type, probe, probe, probe, probe>) {
        auto &[p1, p2, p3, p4] = object;
        return std::tie(p1, p2, p3, p4);
    } else if constexpr (IsBracesContructible<type, probe, probe, probe>) {
        auto &[p1, p2, p3] = object;
        return std::tie(p1, p2, p3);
    } else if constexpr (IsBracesContructible<type, probe, probe>) {
        auto &[p1, p2] = object;
        return std::tie(p1, p2);
    } else if constexpr (IsBracesContructible<type, probe>) {
        auto &[p1] = object;
        return std::tie(p1);
    } else {
        static_assert(std::is_empty_v<type>,
                      "Could not reflect this non-empty aggregate with the generated C++20 fallback. "
                      "Its member count may be outside CBOR_TAGS_REFLECTION_RANGES, or its members may require "
                      "incompatible value and reference probes. Use a matching generated range, native reflection, "
                      "or a custom encoder/decoder.");
        return std::make_tuple();
    }
}

namespace detail {
template <typename T>
    requires IsAggregate<T> || IsTuple<T>
constexpr auto aggregate_binding_count = []() consteval {
    using type = std::remove_cvref_t<T>;
    if constexpr (IsTuple<type>) {
        return std::tuple_size_v<type>;
    } else {
        return std::tuple_size_v<decltype(to_tuple(std::declval<type &>()))>;
    }
}();
} // namespace detail

} // namespace cbor::tags

#endif
