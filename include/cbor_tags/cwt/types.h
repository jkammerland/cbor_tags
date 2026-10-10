#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_integer.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace cbor::tags::cwt {

using byte_string    = std::vector<std::byte>;
using numeric_date   = std::variant<std::int64_t, double>;
using audience_claim = std::variant<std::string, std::vector<std::string>>;
using audience_view  = std::variant<std::string_view, std::vector<std::string_view>>;

inline constexpr std::uint64_t cwt_tag_value        = 61;
inline constexpr std::uint64_t cose_sign1_tag_value = 18;
inline constexpr std::uint64_t cose_sign_tag_value  = 98;

using cwt_tag        = static_tag<cwt_tag_value>;
using cose_sign1_tag = static_tag<cose_sign1_tag_value>;
using cose_sign_tag  = static_tag<cose_sign_tag_value>;

// Wire identifiers; built-in signing/verification currently implements ES256 only.
// Other algorithms can be supplied through the Backend template parameter.
enum class algorithm : std::int64_t {
    es256 = -7,
    es384 = -35,
    es512 = -36,
};

using header_label = std::variant<integer, std::string>;

using byte_view         = std::span<const std::byte>;
using header_label_view = std::variant<integer, std::string_view>;

namespace detail {

// Storage selection is internal; the public model templates are plain aggregates.
// Borrowed text and bytes refer to the input buffer or projected owner. Vectors
// own only their descriptors, so views may still allocate metadata.
template <bool Borrowed> using text_storage     = std::conditional_t<Borrowed, std::string_view, std::string>;
template <bool Borrowed> using byte_storage     = std::conditional_t<Borrowed, byte_view, byte_string>;
template <bool Borrowed> using audience_storage = std::conditional_t<Borrowed, audience_view, audience_claim>;
template <bool Borrowed> using label_storage    = std::variant<integer, text_storage<Borrowed>>;

} // namespace detail

template <bool Borrowed> struct basic_header_map {
    std::optional<algorithm>                      alg;
    std::optional<detail::byte_storage<Borrowed>> kid;
    std::vector<detail::label_storage<Borrowed>>  crit;

    [[nodiscard]] constexpr bool empty() const noexcept { return !alg && !kid && crit.empty(); }
};

template <bool Borrowed> struct basic_claims_set {
    std::optional<detail::text_storage<Borrowed>>     issuer;
    std::optional<detail::text_storage<Borrowed>>     subject;
    std::optional<detail::audience_storage<Borrowed>> audience;
    std::optional<numeric_date>                       expiration;
    std::optional<numeric_date>                       not_before;
    std::optional<numeric_date>                       issued_at;
    std::optional<detail::byte_storage<Borrowed>>     cwt_id;
};

template <bool Borrowed> struct basic_cose_signature {
    detail::byte_storage<Borrowed> protected_header;
    basic_header_map<Borrowed>     unprotected;
    detail::byte_storage<Borrowed> signature;
};

template <bool Borrowed> struct basic_cose_sign {
    detail::byte_storage<Borrowed>                protected_header;
    basic_header_map<Borrowed>                    unprotected;
    std::optional<detail::byte_storage<Borrowed>> payload;
    std::vector<basic_cose_signature<Borrowed>>   signatures;
};

template <bool Borrowed> struct basic_cose_sign1 {
    detail::byte_storage<Borrowed>                protected_header;
    basic_header_map<Borrowed>                    unprotected;
    std::optional<detail::byte_storage<Borrowed>> payload;
    detail::byte_storage<Borrowed>                signature;
};

template <bool Borrowed> struct basic_sig_structure {
    detail::text_storage<Borrowed>                context;
    detail::byte_storage<Borrowed>                body_protected;
    std::optional<detail::byte_storage<Borrowed>> sign_protected;
    detail::byte_storage<Borrowed>                external_aad;
    detail::byte_storage<Borrowed>                payload;
};

namespace detail {

template <typename T> struct model {
    static constexpr bool supported = false;
};
template <bool Borrowed> struct model<basic_header_map<Borrowed>> {
    static constexpr bool supported = true;
    static constexpr bool borrowed  = Borrowed;
};
template <bool Borrowed> struct model<basic_claims_set<Borrowed>> {
    static constexpr bool supported = true;
    static constexpr bool borrowed  = Borrowed;
};
template <bool Borrowed> struct model<basic_cose_signature<Borrowed>> {
    static constexpr bool supported = true;
    static constexpr bool borrowed  = Borrowed;
};
template <bool Borrowed> struct model<basic_cose_sign<Borrowed>> {
    static constexpr bool supported = true;
    static constexpr bool borrowed  = Borrowed;
};
template <bool Borrowed> struct model<basic_cose_sign1<Borrowed>> {
    static constexpr bool supported = true;
    static constexpr bool borrowed  = Borrowed;
};
template <bool Borrowed> struct model<basic_sig_structure<Borrowed>> {
    static constexpr bool supported = true;
    static constexpr bool borrowed  = Borrowed;
};

template <typename T>
concept Model = model<std::remove_cvref_t<T>>::supported;

} // namespace detail

// These ADL guards are reached only when codec::cwt was omitted. The codec's
// specific overloads take precedence, including its one-argument decode path.
template <typename Encoder, detail::Model T> auto encode(Encoder &, const T &) -> typename Encoder::expected_type {
    static_assert(always_false<T>::value, "CWT types require codec::cwt");
    return {};
}
template <typename Decoder, detail::Model T> auto decode(Decoder &, T &&) -> typename Decoder::expected_type {
    static_assert(always_false<T>::value, "CWT types require codec::cwt");
    return {};
}

using header_map          = basic_header_map<false>;
using header_map_view     = basic_header_map<true>;
using claims_set          = basic_claims_set<false>;
using claims_view         = basic_claims_set<true>;
using cose_signature      = basic_cose_signature<false>;
using cose_signature_view = basic_cose_signature<true>;
using cose_sign           = basic_cose_sign<false>;
using cose_sign_view      = basic_cose_sign<true>;
using cose_sign1          = basic_cose_sign1<false>;
using cose_sign1_view     = basic_cose_sign1<true>;
using sig_structure       = basic_sig_structure<false>;
using sig_structure_view  = basic_sig_structure<true>;

namespace detail {
template <typename T, typename Fn> auto view_optional(const std::optional<T> &value, Fn project) {
    using V = decltype(project(*value));
    return value ? std::optional<V>{project(*value)} : std::optional<V>{};
}

inline audience_view view_audience(const audience_claim &audience) {
    return std::visit(
        [](const auto &item) -> audience_view {
            if constexpr (std::same_as<std::remove_cvref_t<decltype(item)>, std::string>) {
                return std::string_view{item};
            } else {
                std::vector<std::string_view> views;
                views.reserve(item.size());
                for (const auto &text : item) {
                    views.emplace_back(text);
                }
                return views;
            }
        },
        audience);
}
} // namespace detail

[[nodiscard]] inline header_map_view as_view(const header_map &value) {
    header_map_view result{.alg  = value.alg,
                           .kid  = detail::view_optional(value.kid, [](const auto &bytes) { return byte_view{bytes}; }),
                           .crit = {}};
    result.crit.reserve(value.crit.size());
    for (const auto &label : value.crit) {
        result.crit.push_back(std::visit(
            [](const auto &item) -> header_label_view {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(item)>, integer>) {
                    return item;
                } else {
                    return std::string_view{item};
                }
            },
            label));
    }
    return result;
}

[[nodiscard]] inline claims_view as_view(const claims_set &value) {
    const auto text = [](const auto &s) { return std::string_view{s}; };
    return {.issuer     = detail::view_optional(value.issuer, text),
            .subject    = detail::view_optional(value.subject, text),
            .audience   = detail::view_optional(value.audience, detail::view_audience),
            .expiration = value.expiration,
            .not_before = value.not_before,
            .issued_at  = value.issued_at,
            .cwt_id     = detail::view_optional(value.cwt_id, [](const auto &bytes) { return byte_view{bytes}; })};
}

[[nodiscard]] inline cose_signature_view as_view(const cose_signature &value) {
    return {.protected_header = value.protected_header, .unprotected = as_view(value.unprotected), .signature = value.signature};
}

[[nodiscard]] inline cose_sign_view as_view(const cose_sign &value) {
    cose_sign_view result{.protected_header = value.protected_header,
                          .unprotected      = as_view(value.unprotected),
                          .payload          = detail::view_optional(value.payload, [](const auto &bytes) { return byte_view{bytes}; }),
                          .signatures       = {}};
    result.signatures.reserve(value.signatures.size());
    for (const auto &signature : value.signatures)
        result.signatures.push_back(as_view(signature));
    return result;
}

[[nodiscard]] inline cose_sign1_view as_view(const cose_sign1 &value) {
    return {.protected_header = value.protected_header,
            .unprotected      = as_view(value.unprotected),
            .payload          = detail::view_optional(value.payload, [](const auto &bytes) { return byte_view{bytes}; }),
            .signature        = value.signature};
}

[[nodiscard]] inline sig_structure_view as_view(const sig_structure &value) {
    return {.context        = value.context,
            .body_protected = value.body_protected,
            .sign_protected = detail::view_optional(value.sign_protected, [](const auto &bytes) { return byte_view{bytes}; }),
            .external_aad   = value.external_aad,
            .payload        = value.payload};
}

template <detail::Model T>
    requires(!std::is_lvalue_reference_v<T>)
void as_view(T &&) = delete;

} // namespace cbor::tags::cwt
