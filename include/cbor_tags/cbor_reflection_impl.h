#pragma once

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_detail.h"

#include <tuple>
#include <type_traits>

namespace cbor::tags {

namespace detail {
constexpr size_t MAX_REFLECTION_MEMBERS = 24;
} // namespace detail

template <class T> constexpr auto to_tuple(T &&object) noexcept {
    using type = std::decay_t<T>;
    static_assert(IsAggregate<type>, "Type must be an aggregate");
    static_assert(detail::aggregate_binding_count<type> <= detail::MAX_REFLECTION_MEMBERS, "Type must have at most 24 members. Rerun the generator with a higher value if you need more.");
    

    if constexpr (IsTuple<type>) {
        return; // unreachable due to IsAggregate
    } else if constexpr (IsBracesContructible<type, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24);
    } else if constexpr (IsBracesContructible<type, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23);
    } else if constexpr (IsBracesContructible<type, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22);
    } else if constexpr (IsBracesContructible<type, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21);
    } else if constexpr (IsBracesContructible<type, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20);
    } else if constexpr (IsBracesContructible<type, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19);
    } else if constexpr (IsBracesContructible<type, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18);
    } else if constexpr (IsBracesContructible<type, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17);
    } else if constexpr (IsBracesContructible<type, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16);
    } else if constexpr (IsBracesContructible<type, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15);
    } else if constexpr (IsBracesContructible<type, any, any, any, any, any, any, any, any, any, any, any, any, any, any>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14);
    } else if constexpr (IsBracesContructible<type, any, any, any, any, any, any, any, any, any, any, any, any, any>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13);
    } else if constexpr (IsBracesContructible<type, any, any, any, any, any, any, any, any, any, any, any, any>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12);
    } else if constexpr (IsBracesContructible<type, any, any, any, any, any, any, any, any, any, any, any>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11);
    } else if constexpr (IsBracesContructible<type, any, any, any, any, any, any, any, any, any, any>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10);
    } else if constexpr (IsBracesContructible<type, any, any, any, any, any, any, any, any, any>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9);
    } else if constexpr (IsBracesContructible<type, any, any, any, any, any, any, any, any>) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8);
    } else if constexpr (IsBracesContructible<type, any, any, any, any, any, any, any>) {
        auto &[p1, p2, p3, p4, p5, p6, p7] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7);
    } else if constexpr (IsBracesContructible<type, any, any, any, any, any, any>) {
        auto &[p1, p2, p3, p4, p5, p6] = object;
        return std::tie(p1, p2, p3, p4, p5, p6);
    } else if constexpr (IsBracesContructible<type, any, any, any, any, any>) {
        auto &[p1, p2, p3, p4, p5] = object;
        return std::tie(p1, p2, p3, p4, p5);
    } else if constexpr (IsBracesContructible<type, any, any, any, any>) {
        auto &[p1, p2, p3, p4] = object;
        return std::tie(p1, p2, p3, p4);
    } else if constexpr (IsBracesContructible<type, any, any, any>) {
        auto &[p1, p2, p3] = object;
        return std::tie(p1, p2, p3);
    } else if constexpr (IsBracesContructible<type, any, any>) {
        auto &[p1, p2] = object;
        return std::tie(p1, p2);
    } else if constexpr (IsBracesContructible<type, any>) {
        auto &[p1] = object;
        return std::tie(p1);
    } else {
        return std::make_tuple();
    }
}

} // namespace cbor::tags
