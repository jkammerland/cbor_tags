#pragma once

#include "cwt_test_support.h"

#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
#include <cbor_tags/cwt/openssl_crypto.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/rsa.h>
#endif

namespace cwt_test {

#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
using crypto_es256_backend = openssl_es256_backend;

struct evp_pkey_deleter {
    void operator()(EVP_PKEY *key) const noexcept {
        if (key != nullptr) {
            EVP_PKEY_free(key);
        }
    }
};

struct evp_pkey_ctx_deleter {
    void operator()(EVP_PKEY_CTX *context) const noexcept {
        if (context != nullptr) {
            (void)EVP_PKEY_CTX_free(context);
        }
    }
};

using evp_pkey_ptr     = std::unique_ptr<EVP_PKEY, evp_pkey_deleter>;
using evp_pkey_ctx_ptr = std::unique_ptr<EVP_PKEY_CTX, evp_pkey_ctx_deleter>;

inline evp_pkey_ptr make_ec_key(int curve_nid) {
    evp_pkey_ctx_ptr context{EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr)};
    REQUIRE(context);
    REQUIRE(EVP_PKEY_keygen_init(context.get()) == 1);
    REQUIRE(EVP_PKEY_CTX_set_ec_paramgen_curve_nid(context.get(), curve_nid) == 1);

    EVP_PKEY *raw_key{};
    REQUIRE(EVP_PKEY_keygen(context.get(), &raw_key) == 1);
    return evp_pkey_ptr{raw_key};
}

inline evp_pkey_ptr make_p256_key() { return make_ec_key(NID_X9_62_prime256v1); }

inline evp_pkey_ptr make_rsa_key() {
    evp_pkey_ctx_ptr context{EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr)};
    REQUIRE(context);
    REQUIRE(EVP_PKEY_keygen_init(context.get()) == 1);
    REQUIRE(EVP_PKEY_CTX_set_rsa_keygen_bits(context.get(), 1024) == 1);

    EVP_PKEY *raw_key{};
    REQUIRE(EVP_PKEY_keygen(context.get(), &raw_key) == 1);
    return evp_pkey_ptr{raw_key};
}
#endif

} // namespace cwt_test
