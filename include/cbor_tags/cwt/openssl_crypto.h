#pragma once

// clang-format off
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>

#include "cbor_tags/cwt/detail/openssl_compat_es256.h"
// clang-format on

namespace cbor::tags::cwt {

struct openssl_es256_backend {
    static constexpr algorithm algorithm_id = algorithm::es256;

    template <detail::SignatureParts Parts>
    [[nodiscard]] static expected<std::vector<std::byte>, status_code> sign(EVP_PKEY *key, Parts &&parts) {
        return detail::openssl_compat_es256_sign(key, std::forward<Parts>(parts));
    }

    template <detail::SignatureParts Parts>
    [[nodiscard]] static expected<void, status_code> verify(EVP_PKEY *key, Parts &&parts, std::span<const std::byte> signature) {
        return detail::openssl_compat_es256_verify(key, std::forward<Parts>(parts), signature);
    }
};

} // namespace cbor::tags::cwt
