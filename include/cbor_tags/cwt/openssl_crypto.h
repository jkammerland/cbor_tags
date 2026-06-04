#pragma once

#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>

#include "cbor_tags/cwt/detail/openssl_compat_es256.h"

namespace cbor::tags::cwt {

struct openssl_es256_backend {
    static constexpr algorithm algorithm_id = algorithm::es256;

    [[nodiscard]] static expected<byte_string, status_code> sign(EVP_PKEY *key, std::span<const std::byte> to_be_signed) {
        return detail::openssl_compat_es256_sign(key, to_be_signed);
    }

    [[nodiscard]] static expected<void, status_code> verify(EVP_PKEY *key, std::span<const std::byte> to_be_signed,
                                                            std::span<const std::byte> signature) {
        return detail::openssl_compat_es256_verify(key, to_be_signed, signature);
    }
};

} // namespace cbor::tags::cwt
