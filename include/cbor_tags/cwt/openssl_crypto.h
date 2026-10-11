#pragma once

// clang-format off
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>

#include "cbor_tags/cwt/detail/openssl_compat_es256.h"
// clang-format on

namespace cbor::tags::cwt {

struct openssl_esp256_backend {
    static constexpr algorithm algorithm_id = algorithm::esp256;

    template <detail::SignatureParts Parts>
    [[nodiscard]] static expected<std::vector<std::byte>, status_code> sign(EVP_PKEY *key, Parts &&parts) {
        std::vector<std::byte> signature(64);
        auto                   result = sign(key, std::forward<Parts>(parts), signature);
        if (!result) {
            return unexpected<status_code>{result.error()};
        }
        return signature;
    }

    template <detail::SignatureParts Parts>
    [[nodiscard]] static expected<std::size_t, status_code> sign(EVP_PKEY *key, Parts &&parts, std::span<std::byte> output) {
        return detail::openssl_compat_es256_sign(key, std::forward<Parts>(parts), output);
    }

    template <typename Key, detail::SignatureParts Parts>
        requires requires(const Key &key) {
            key.get();
            key.algorithm_id();
        }
    [[nodiscard]] static expected<void, status_code> verify(const Key &key, Parts &&parts, std::span<const std::byte> signature) {
        if (key.algorithm_id() != algorithm_id)
            return unexpected<status_code>{status_code::error};
        return verify(key.get(), std::forward<Parts>(parts), signature);
    }

    template <detail::SignatureParts Parts>
    [[nodiscard]] static expected<void, status_code> verify(EVP_PKEY *key, Parts &&parts, std::span<const std::byte> signature) {
        return detail::openssl_compat_es256_verify(key, std::forward<Parts>(parts), signature);
    }
};

// Explicit compatibility verification; new tokens use ESP256.
struct openssl_es256_verifier {
    static constexpr algorithm algorithm_id = algorithm::es256;

    template <typename Key, detail::SignatureParts Parts>
        requires requires(const Key &key) {
            key.get();
            key.algorithm_id();
        }
    [[nodiscard]] static expected<void, status_code> verify(const Key &key, Parts &&parts, std::span<const std::byte> signature) {
        if (key.algorithm_id() != algorithm_id)
            return unexpected<status_code>{status_code::error};
        return verify(key.get(), std::forward<Parts>(parts), signature);
    }

    template <detail::SignatureParts Parts>
    [[nodiscard]] static expected<void, status_code> verify(EVP_PKEY *key, Parts &&parts, std::span<const std::byte> signature) {
        return detail::openssl_compat_es256_verify(key, std::forward<Parts>(parts), signature);
    }
};

} // namespace cbor::tags::cwt
