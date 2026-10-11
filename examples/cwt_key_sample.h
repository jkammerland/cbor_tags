#pragma once

#include <cbor_tags/cwt/openssl_sha256.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>

// Generate a temporary issuer key for the runnable examples.
inline std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> make_issuer_key() {
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> context{
        EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr), EVP_PKEY_CTX_free};
    if (!context || EVP_PKEY_keygen_init(context.get()) != 1 ||
        EVP_PKEY_CTX_set_ec_paramgen_curve_nid(context.get(), NID_X9_62_prime256v1) != 1) {
        return {nullptr, EVP_PKEY_free};
    }

    EVP_PKEY *key{};
    if (EVP_PKEY_keygen(context.get(), &key) != 1) {
        EVP_PKEY_free(key);
        return {nullptr, EVP_PKEY_free};
    }
    return {key, EVP_PKEY_free};
}

// Use the public key from RFC 9679 section 6 as a separately provisioned holder key.
inline cbor::tags::cwt::ec2_public_key example_holder_key() {
    using namespace cbor::tags;
    cwt::ec2_public_key key;
    key.x = {std::byte{0x65}, std::byte{0xed}, std::byte{0xa5}, std::byte{0xa1}, std::byte{0x25},
        std::byte{0x77}, std::byte{0xc2}, std::byte{0xba}, std::byte{0xe8}, std::byte{0x29},
        std::byte{0x43}, std::byte{0x7f}, std::byte{0xe3}, std::byte{0x38}, std::byte{0x70},
        std::byte{0x1a}, std::byte{0x10}, std::byte{0xaa}, std::byte{0xa3}, std::byte{0x75},
        std::byte{0xe1}, std::byte{0xbb}, std::byte{0x5b}, std::byte{0x5d}, std::byte{0xe1},
        std::byte{0x08}, std::byte{0xde}, std::byte{0x43}, std::byte{0x9c}, std::byte{0x08},
        std::byte{0x55}, std::byte{0x1d}};
    key.y = std::vector<std::byte>{std::byte{0x1e}, std::byte{0x52}, std::byte{0xed},
        std::byte{0x75}, std::byte{0x70}, std::byte{0x11}, std::byte{0x63}, std::byte{0xf7},
        std::byte{0xf9}, std::byte{0xe4}, std::byte{0x0d}, std::byte{0xdf}, std::byte{0x9f},
        std::byte{0x34}, std::byte{0x1b}, std::byte{0x3d}, std::byte{0xc9}, std::byte{0xba},
        std::byte{0x86}, std::byte{0x0a}, std::byte{0xf7}, std::byte{0xe0}, std::byte{0xca},
        std::byte{0x7c}, std::byte{0xa7}, std::byte{0xe9}, std::byte{0xee}, std::byte{0xcd},
        std::byte{0x00}, std::byte{0x84}, std::byte{0xd1}, std::byte{0x9c}};
    key.alg = cwt::algorithm::esp256;
    key.key_ops = std::vector<std::int64_t>{2};
    return key;
}
