#pragma once

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_reflection_count.h"

#include <tuple>
#include <type_traits>

#if !CBOR_TAGS_HAS_STD_REFLECTION && !CBOR_TAGS_HAS_BOOST_PFR_NAMES

namespace cbor::tags {

namespace detail {
constexpr size_t MAX_REFLECTION_MEMBERS = 24;
static_assert(MAX_REFLECTION_MEMBERS == MAX_AGGREGATE_PROBE_MEMBERS,
              "to_tuple(...) dispatches on aggregate_binding_count, so the generated arities and the probed arity "
              "must match. Rerun tools/reflection_module_generator with the same bound, or update "
              "MAX_AGGREGATE_PROBE_MEMBERS in cbor_reflection_count.h.");
} // namespace detail

template <class T> constexpr auto to_tuple(T &&object) noexcept {
    using type = std::decay_t<T>;
    static_assert(IsAggregate<type>, "Type must be an aggregate");
    static_assert(detail::aggregate_binding_count<type> != detail::UNDETECTABLE_AGGREGATE_ARITY,
                  "Could not safely determine this aggregate's member count with the C++20 fallback. "
                  "An aggregate that combines mutable reference members with non-trivially-copyable value members "
                  "requires native C++ reflection or a custom encoder/decoder.");
    static_assert(detail::aggregate_binding_count<type> <= detail::MAX_REFLECTION_MEMBERS, "Type must have at most 24 members. Rerun the generator with a higher value if you need more.");
    

    if constexpr (IsTuple<type>) {
        return; // unreachable due to IsAggregate
    } else if constexpr (detail::aggregate_binding_count<type> == 24) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23, p24);
    } else if constexpr (detail::aggregate_binding_count<type> == 23) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22, p23);
    } else if constexpr (detail::aggregate_binding_count<type> == 22) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21, p22);
    } else if constexpr (detail::aggregate_binding_count<type> == 21) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20, p21);
    } else if constexpr (detail::aggregate_binding_count<type> == 20) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19, p20);
    } else if constexpr (detail::aggregate_binding_count<type> == 19) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18, p19);
    } else if constexpr (detail::aggregate_binding_count<type> == 18) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17, p18);
    } else if constexpr (detail::aggregate_binding_count<type> == 17) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16, p17);
    } else if constexpr (detail::aggregate_binding_count<type> == 16) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15, p16);
    } else if constexpr (detail::aggregate_binding_count<type> == 15) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14, p15);
    } else if constexpr (detail::aggregate_binding_count<type> == 14) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13, p14);
    } else if constexpr (detail::aggregate_binding_count<type> == 13) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12, p13);
    } else if constexpr (detail::aggregate_binding_count<type> == 12) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, p12);
    } else if constexpr (detail::aggregate_binding_count<type> == 11) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11);
    } else if constexpr (detail::aggregate_binding_count<type> == 10) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9, p10] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9, p10);
    } else if constexpr (detail::aggregate_binding_count<type> == 9) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8, p9] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8, p9);
    } else if constexpr (detail::aggregate_binding_count<type> == 8) {
        auto &[p1, p2, p3, p4, p5, p6, p7, p8] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7, p8);
    } else if constexpr (detail::aggregate_binding_count<type> == 7) {
        auto &[p1, p2, p3, p4, p5, p6, p7] = object;
        return std::tie(p1, p2, p3, p4, p5, p6, p7);
    } else if constexpr (detail::aggregate_binding_count<type> == 6) {
        auto &[p1, p2, p3, p4, p5, p6] = object;
        return std::tie(p1, p2, p3, p4, p5, p6);
    } else if constexpr (detail::aggregate_binding_count<type> == 5) {
        auto &[p1, p2, p3, p4, p5] = object;
        return std::tie(p1, p2, p3, p4, p5);
    } else if constexpr (detail::aggregate_binding_count<type> == 4) {
        auto &[p1, p2, p3, p4] = object;
        return std::tie(p1, p2, p3, p4);
    } else if constexpr (detail::aggregate_binding_count<type> == 3) {
        auto &[p1, p2, p3] = object;
        return std::tie(p1, p2, p3);
    } else if constexpr (detail::aggregate_binding_count<type> == 2) {
        auto &[p1, p2] = object;
        return std::tie(p1, p2);
    } else if constexpr (detail::aggregate_binding_count<type> == 1) {
        auto &[p1] = object;
        return std::tie(p1);
    } else {
        static_assert(std::is_empty_v<type>,
                      "Could not determine the member count for this aggregate. Members that are "
                      "C arrays, or an aggregate with a base class, are not supported.");
        return std::make_tuple();
    }
}

} // namespace cbor::tags

#endif
