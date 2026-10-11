#pragma once

#include "cbor_tags/cwt/types.h"

#include <algorithm>

namespace cbor::tags::cwt {

template <bool Borrowed> struct basic_key_metadata {
    std::optional<detail::byte_storage<Borrowed>> kid;
    std::optional<algorithm>                      alg;
    std::optional<std::vector<std::int64_t>>      key_ops;
};

template <bool Borrowed> struct basic_ec2_public_key : basic_key_metadata<Borrowed> {
    static constexpr std::int64_t                              key_type = 2;
    static constexpr bool                                      borrowed = Borrowed;
    std::variant<std::int64_t, detail::text_storage<Borrowed>> curve{1};
    detail::byte_storage<Borrowed>                             x;
    std::variant<detail::byte_storage<Borrowed>, bool>         y;
};

template <bool Borrowed> struct basic_okp_public_key : basic_key_metadata<Borrowed> {
    static constexpr std::int64_t                              key_type = 1;
    static constexpr bool                                      borrowed = Borrowed;
    std::variant<std::int64_t, detail::text_storage<Borrowed>> curve{6};
    detail::byte_storage<Borrowed>                             x;
};

template <bool Borrowed> struct basic_rsa_public_key : basic_key_metadata<Borrowed> {
    static constexpr std::int64_t  key_type = 3;
    static constexpr bool          borrowed = Borrowed;
    detail::byte_storage<Borrowed> modulus;
    detail::byte_storage<Borrowed> exponent;
};

using ec2_public_key      = basic_ec2_public_key<false>;
using ec2_public_key_view = basic_ec2_public_key<true>;
using okp_public_key      = basic_okp_public_key<false>;
using okp_public_key_view = basic_okp_public_key<true>;
using rsa_public_key      = basic_rsa_public_key<false>;
using rsa_public_key_view = basic_rsa_public_key<true>;

namespace detail {
template <typename T> struct public_key_model : std::false_type {};
template <bool B> struct public_key_model<basic_ec2_public_key<B>> : std::true_type {};
template <bool B> struct public_key_model<basic_okp_public_key<B>> : std::true_type {};
template <bool B> struct public_key_model<basic_rsa_public_key<B>> : std::true_type {};
template <typename T>
concept PublicKey = public_key_model<std::remove_cvref_t<T>>::value;

template <PublicKey Key> bool valid_public_key(const Key &key) {
    if constexpr (Key::key_type == 3) {
        return !key.modulus.empty() && !key.exponent.empty() && key.modulus.front() != std::byte{} && key.exponent.front() != std::byte{};
    } else {
        if (key.x.empty())
            return false;
        if (const auto *curve = std::get_if<std::int64_t>(&key.curve)) {
            std::size_t size{};
            if constexpr (Key::key_type == 2) {
                if (*curve >= 4 && *curve <= 7)
                    return false;
                if (*curve == 1)
                    size = 32;
                if (*curve == 2)
                    size = 48;
                if (*curve == 3)
                    size = 66;
            } else {
                if (*curve >= 1 && *curve <= 3)
                    return false;
                if (*curve == 4 || *curve == 6)
                    size = 32;
                if (*curve == 5)
                    size = 56;
                if (*curve == 7)
                    size = 57;
            }
            if (size && key.x.size() != size)
                return false;
        }
        if constexpr (Key::key_type == 2) {
            const auto *y = std::get_if<byte_storage<Key::borrowed>>(&key.y);
            if (y && y->size() != key.x.size())
                return false;
        }
        return true;
    }
}
} // namespace detail

// Key metadata is an admission restriction, not a cryptographic validity proof.
template <detail::PublicKey Key>
[[nodiscard]] expected<void, status_code> validate_key_use(const Key &key, algorithm alg, std::int64_t operation = 2) {
    if (!detail::valid_public_key(key) || (key.alg && *key.alg != alg))
        return unexpected<status_code>{status_code::error};
    if (key.key_ops && std::ranges::find(*key.key_ops, operation) == key.key_ops->end())
        return unexpected<status_code>{status_code::error};
    return {};
}

namespace detail {
template <typename View, typename Key> void view_key_metadata(View &view, const Key &key) {
    view.alg     = key.alg;
    view.key_ops = key.key_ops;
    if (key.kid)
        view.kid = std::span<const std::byte>{*key.kid};
}
inline auto view_curve(const std::variant<std::int64_t, std::string> &curve) {
    return std::visit([](const auto &value) -> std::variant<std::int64_t, std::string_view> { return value; }, curve);
}
} // namespace detail

inline ec2_public_key_view as_view(const ec2_public_key &key) {
    ec2_public_key_view view;
    detail::view_key_metadata(view, key);
    view.curve = detail::view_curve(key.curve);
    view.x     = key.x;
    if (const auto *parity = std::get_if<bool>(&key.y))
        view.y = *parity;
    else
        view.y = std::span<const std::byte>{std::get<std::vector<std::byte>>(key.y)};
    return view;
}
inline okp_public_key_view as_view(const okp_public_key &key) {
    okp_public_key_view view;
    detail::view_key_metadata(view, key);
    view.curve = detail::view_curve(key.curve);
    view.x     = key.x;
    return view;
}
inline rsa_public_key_view as_view(const rsa_public_key &key) {
    rsa_public_key_view view;
    detail::view_key_metadata(view, key);
    view.modulus  = key.modulus;
    view.exponent = key.exponent;
    return view;
}
inline auto as_view(const ec2_public_key &&) = delete;
inline auto as_view(const okp_public_key &&) = delete;
inline auto as_view(const rsa_public_key &&) = delete;

namespace detail {
template <bool B> struct model<basic_ec2_public_key<B>> {
    static constexpr bool supported = true;
    static constexpr bool borrowed  = B;
};
template <bool B> struct model<basic_okp_public_key<B>> {
    static constexpr bool supported = true;
    static constexpr bool borrowed  = B;
};
template <bool B> struct model<basic_rsa_public_key<B>> {
    static constexpr bool supported = true;
    static constexpr bool borrowed  = B;
};
} // namespace detail
} // namespace cbor::tags::cwt
