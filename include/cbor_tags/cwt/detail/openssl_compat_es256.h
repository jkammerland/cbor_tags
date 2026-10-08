#pragma once

#include "cbor_tags/cwt/cwt.h"

#include <openssl/obj_mac.h>
#include <openssl/objects.h>
#include <openssl/opensslv.h>
#if OPENSSL_VERSION_NUMBER >= 0x30000000L && !defined(LIBRESSL_VERSION_NUMBER)
#include <openssl/core_names.h>
#else
#include <openssl/ec.h>
#endif

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace cbor::tags::cwt::detail {

template <typename T, void (*FreeFn)(T *)> using ossl_ptr = std::unique_ptr<T, decltype(FreeFn)>;

[[nodiscard]] inline expected<void, status_code> validate_openssl_es256_key(EVP_PKEY *key) {
    if (key == nullptr) {
        return unexpected<status_code>{status_code::error};
    }

#if OPENSSL_VERSION_NUMBER >= 0x30000000L && !defined(LIBRESSL_VERSION_NUMBER)
    if (EVP_PKEY_is_a(key, "EC") != 1) {
        return unexpected<status_code>{status_code::error};
    }

    char        group_name[80]{};
    std::size_t group_name_size{};
    if (EVP_PKEY_get_utf8_string_param(key, OSSL_PKEY_PARAM_GROUP_NAME, group_name, sizeof(group_name), &group_name_size) != 1 ||
        OBJ_txt2nid(group_name) != NID_X9_62_prime256v1) {
        return unexpected<status_code>{status_code::error};
    }
#else
    if (EVP_PKEY_base_id(key) != EVP_PKEY_EC) {
        return unexpected<status_code>{status_code::error};
    }

    const auto *ec_key = EVP_PKEY_get0_EC_KEY(key);
    if (ec_key == nullptr) {
        return unexpected<status_code>{status_code::error};
    }
    const auto *group = EC_KEY_get0_group(ec_key);
    if (group == nullptr || EC_GROUP_get_curve_name(group) != NID_X9_62_prime256v1) {
        return unexpected<status_code>{status_code::error};
    }
#endif

    return {};
}

[[nodiscard]] inline expected<void, status_code> bn_to_fixed_bytes(const BIGNUM *value, std::span<std::byte> output) {
    const auto value_size = BN_num_bytes(value);
    if (value_size < 0 || static_cast<std::size_t>(value_size) > output.size()) {
        return unexpected<status_code>{status_code::error};
    }

    const auto padding_size = output.size() - static_cast<std::size_t>(value_size);
    for (auto &byte : output.subspan(0U, padding_size)) {
        byte = std::byte{0};
    }

    if (value_size == 0) {
        return {};
    }

    const auto written = BN_bn2bin(value, reinterpret_cast<unsigned char *>(output.data() + padding_size));
    if (written != value_size) {
        return unexpected<status_code>{status_code::error};
    }
    return {};
}

[[nodiscard]] inline expected<byte_string, status_code> ecdsa_der_to_raw_es256(std::span<const std::byte> der_signature) {
    const auto *cursor = reinterpret_cast<const unsigned char *>(der_signature.data());
    const auto *end    = cursor + der_signature.size();

    ossl_ptr<ECDSA_SIG, ECDSA_SIG_free> signature{d2i_ECDSA_SIG(nullptr, &cursor, static_cast<long>(der_signature.size())), ECDSA_SIG_free};
    if (!signature || cursor != end) {
        return unexpected<status_code>{status_code::error};
    }

    const BIGNUM *r{};
    const BIGNUM *s{};
    ECDSA_SIG_get0(signature.get(), &r, &s);
    if (r == nullptr || s == nullptr || BN_num_bytes(r) > 32 || BN_num_bytes(s) > 32) {
        return unexpected<status_code>{status_code::error};
    }

    byte_string raw(64);
    auto        r_result = bn_to_fixed_bytes(r, std::span<std::byte>{raw.data(), 32U});
    if (!r_result) {
        return unexpected<status_code>{r_result.error()};
    }
    auto s_result = bn_to_fixed_bytes(s, std::span<std::byte>{raw.data() + 32, 32U});
    if (!s_result) {
        return unexpected<status_code>{s_result.error()};
    }
    return raw;
}

[[nodiscard]] inline expected<std::vector<unsigned char>, status_code> ecdsa_raw_es256_to_der(std::span<const std::byte> raw_signature) {
    if (raw_signature.size() != 64U) {
        return unexpected<status_code>{status_code::error};
    }

    ossl_ptr<ECDSA_SIG, ECDSA_SIG_free> signature{ECDSA_SIG_new(), ECDSA_SIG_free};
    if (!signature) {
        return unexpected<status_code>{status_code::out_of_memory};
    }

    BIGNUM *r = BN_bin2bn(reinterpret_cast<const unsigned char *>(raw_signature.data()), 32, nullptr);
    BIGNUM *s = BN_bin2bn(reinterpret_cast<const unsigned char *>(raw_signature.data() + 32), 32, nullptr);
    if (r == nullptr || s == nullptr) {
        BN_free(r);
        BN_free(s);
        return unexpected<status_code>{status_code::out_of_memory};
    }
    if (ECDSA_SIG_set0(signature.get(), r, s) != 1) {
        BN_free(r);
        BN_free(s);
        return unexpected<status_code>{status_code::error};
    }

    const auto der_size = i2d_ECDSA_SIG(signature.get(), nullptr);
    if (der_size <= 0) {
        return unexpected<status_code>{status_code::error};
    }

    std::vector<unsigned char> der(static_cast<std::size_t>(der_size));
    auto                      *cursor = der.data();
    if (i2d_ECDSA_SIG(signature.get(), &cursor) != der_size) {
        return unexpected<status_code>{status_code::error};
    }
    return der;
}

template <typename Key>
[[nodiscard]] inline expected<byte_string, status_code> openssl_compat_es256_sign(Key *key, std::span<const std::byte> to_be_signed) {
    auto key_status = validate_openssl_es256_key(key);
    if (!key_status) {
        return unexpected<status_code>{key_status.error()};
    }

    ossl_ptr<EVP_MD_CTX, EVP_MD_CTX_free> context{EVP_MD_CTX_new(), EVP_MD_CTX_free};
    if (!context) {
        return unexpected<status_code>{status_code::out_of_memory};
    }

    if (EVP_DigestSignInit(context.get(), nullptr, EVP_sha256(), nullptr, key) != 1) {
        return unexpected<status_code>{status_code::error};
    }

    std::size_t der_size{};
    const auto *input = reinterpret_cast<const unsigned char *>(to_be_signed.data());
    if (EVP_DigestSignUpdate(context.get(), input, to_be_signed.size()) != 1) {
        return unexpected<status_code>{status_code::error};
    }
    if (EVP_DigestSignFinal(context.get(), nullptr, &der_size) != 1) {
        return unexpected<status_code>{status_code::error};
    }

    std::vector<unsigned char> der(der_size);
    if (EVP_DigestSignFinal(context.get(), der.data(), &der_size) != 1) {
        return unexpected<status_code>{status_code::error};
    }
    der.resize(der_size);

    return ecdsa_der_to_raw_es256(std::as_bytes(std::span<const unsigned char>{der.data(), der.size()}));
}

template <typename Key>
[[nodiscard]] inline expected<void, status_code> openssl_compat_es256_verify(Key *key, std::span<const std::byte> to_be_signed,
                                                                             std::span<const std::byte> signature) {
    auto key_status = validate_openssl_es256_key(key);
    if (!key_status) {
        return unexpected<status_code>{key_status.error()};
    }

    auto der = ecdsa_raw_es256_to_der(signature);
    if (!der) {
        return unexpected<status_code>{der.error()};
    }

    ossl_ptr<EVP_MD_CTX, EVP_MD_CTX_free> context{EVP_MD_CTX_new(), EVP_MD_CTX_free};
    if (!context) {
        return unexpected<status_code>{status_code::out_of_memory};
    }

    if (EVP_DigestVerifyInit(context.get(), nullptr, EVP_sha256(), nullptr, key) != 1) {
        return unexpected<status_code>{status_code::error};
    }

    const auto *input         = reinterpret_cast<const unsigned char *>(to_be_signed.data());
    const auto  update_status = EVP_DigestVerifyUpdate(context.get(), input, to_be_signed.size());
    if (update_status != 1) {
        return unexpected<status_code>{status_code::error};
    }

    const auto verify_status = EVP_DigestVerifyFinal(context.get(), der->data(), der->size());
    if (verify_status != 1) {
        return unexpected<status_code>{status_code::error};
    }
    return {};
}

} // namespace cbor::tags::cwt::detail
