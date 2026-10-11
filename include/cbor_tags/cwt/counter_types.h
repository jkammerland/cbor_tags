#pragma once

#include "cbor_tags/cwt/fields.h"
#include "cbor_tags/cwt/types.h"

namespace cbor::tags::cwt {

// Normalize singleton and nonempty-list encodings without retaining their wire choice.
template <typename Signature, bool Legacy = false> struct counter_collection {
    std::vector<Signature> signatures;
};

// Recursion is selected by the application; ordinary headers remain nonrecursive.
template <bool Borrowed = false, typename Extra = std::monostate> struct counter_headers {
    using signature_type = basic_cose_signature<Borrowed, counter_headers>;

    std::optional<counter_collection<signature_type>>       full;
    std::optional<detail::byte_storage<Borrowed>>           abbreviated;
    std::optional<counter_collection<signature_type, true>> legacy;
    [[no_unique_address]] Extra                             extra{};
};

using counter_headers_view = counter_headers<true>;

template <bool B, typename E> auto cwt_fields(counter_headers<B, E> &value) {
    auto counters = fields(field<11>(value.full), field<12>(value.abbreviated), field<7>(value.legacy));
    if constexpr (std::same_as<E, std::monostate>) {
        return counters;
    } else {
        return fields(counters, cwt_fields(value.extra));
    }
}

template <bool B, typename E> auto cwt_fields(const counter_headers<B, E> &value) {
    auto counters = fields(field<11>(value.full), field<12>(value.abbreviated), field<7>(value.legacy));
    if constexpr (std::same_as<E, std::monostate>) {
        return counters;
    } else {
        return fields(counters, cwt_fields(value.extra));
    }
}

using countersignature_tag = static_tag<19>;

template <bool B, typename E> auto as_countersignature(basic_cose_signature<B, E> value) {
    return make_tag_pair(countersignature_tag{}, std::move(value));
}

} // namespace cbor::tags::cwt
