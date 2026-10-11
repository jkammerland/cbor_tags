#pragma once

#include "cbor_tags/cwt/keys.h"

namespace cbor::tags::cwt {

template <typename Key, bool Borrowed = false> struct basic_confirmation {
    std::optional<Key>                            key;
    std::optional<detail::byte_storage<Borrowed>> kid;
    std::optional<detail::byte_storage<Borrowed>> thumbprint;
};

template <typename Key> using confirmation      = basic_confirmation<Key, false>;
template <typename Key> using confirmation_view = basic_confirmation<Key, true>;

namespace detail {
template <typename Key, bool B> struct model<basic_confirmation<Key, B>> {
    static constexpr bool supported = true;
    static constexpr bool borrowed  = B;
};
} // namespace detail
} // namespace cbor::tags::cwt
