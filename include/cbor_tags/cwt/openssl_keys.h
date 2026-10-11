#pragma once

#include "cbor_tags/cwt/openssl_crypto.h"
#include "cbor_tags/cwt/openssl_sha256.h"

#include <openssl/ec.h>
#if OPENSSL_VERSION_NUMBER >= 0x30000000L && !defined(LIBRESSL_VERSION_NUMBER)
#include <openssl/params.h>
#endif

namespace cbor::tags::cwt {

// This handle admits verification only after checking COSE key restrictions.
class openssl_verification_key {
  public:
    explicit openssl_verification_key(EVP_PKEY *key, algorithm alg) : key_(key, EVP_PKEY_free), algorithm_(alg) {}
    [[nodiscard]] EVP_PKEY *get() const noexcept { return key_.get(); }
    [[nodiscard]] algorithm algorithm_id() const noexcept { return algorithm_; }

  private:
    detail::ossl_ptr<EVP_PKEY, EVP_PKEY_free> key_;
    algorithm                                 algorithm_;
};

namespace detail {
template <bool B> expected<std::array<unsigned char, 65>, status_code> p256_point_bytes(const basic_ec2_public_key<B> &key) {
    const auto *curve = std::get_if<std::int64_t>(&key.curve);
    if (!curve || *curve != 1 || key.x.size() != 32)
        return unexpected<status_code>{status_code::error};

    ossl_ptr<EC_GROUP, EC_GROUP_free> group{EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1), EC_GROUP_free};
    ossl_ptr<EC_POINT, EC_POINT_free> point{group ? EC_POINT_new(group.get()) : nullptr, EC_POINT_free};
    if (!group || !point)
        return unexpected<status_code>{status_code::error};

    std::array<unsigned char, 65> encoded{};
    std::size_t                   size = 33;
    std::transform(key.x.begin(), key.x.end(), encoded.begin() + 1, [](std::byte b) { return std::to_integer<unsigned char>(b); });
    if (const auto *parity = std::get_if<bool>(&key.y))
        encoded[0] = *parity ? 3 : 2;
    else {
        const auto &y = std::get<byte_storage<B>>(key.y);
        if (y.size() != 32)
            return unexpected<status_code>{status_code::error};
        encoded[0] = 4;
        size       = 65;
        std::transform(y.begin(), y.end(), encoded.begin() + 33, [](std::byte b) { return std::to_integer<unsigned char>(b); });
    }
    if (EC_POINT_oct2point(group.get(), point.get(), encoded.data(), size, nullptr) != 1 ||
        EC_POINT_is_on_curve(group.get(), point.get(), nullptr) != 1 || EC_POINT_is_at_infinity(group.get(), point.get()) != 0 ||
        EC_POINT_point2oct(group.get(), point.get(), POINT_CONVERSION_UNCOMPRESSED, encoded.data(), encoded.size(), nullptr) !=
            encoded.size()) {
        return unexpected<status_code>{status_code::error};
    }
    return encoded;
}
} // namespace detail

template <bool B> [[nodiscard]] expected<ec2_public_key, status_code> expand_p256_public_key(const basic_ec2_public_key<B> &key) {
    auto point = detail::p256_point_bytes(key);
    if (!point)
        return unexpected<status_code>{point.error()};

    ec2_public_key expanded;
    expanded.curve   = 1;
    expanded.alg     = key.alg;
    expanded.key_ops = key.key_ops;
    if (key.kid)
        expanded.kid = std::vector<std::byte>{key.kid->begin(), key.kid->end()};
    expanded.x.assign(reinterpret_cast<const std::byte *>(point->data() + 1), reinterpret_cast<const std::byte *>(point->data() + 33));
    expanded.y = std::vector<std::byte>{reinterpret_cast<const std::byte *>(point->data() + 33),
                                        reinterpret_cast<const std::byte *>(point->data() + 65)};
    return expanded;
}

template <bool B>
[[nodiscard]] expected<openssl_verification_key, status_code> import_p256_public_key(const basic_ec2_public_key<B> &key,
                                                                                     algorithm alg = algorithm::esp256) {
    auto permitted = validate_key_use(key, alg);
    if (!permitted || (alg != algorithm::esp256 && alg != algorithm::es256))
        return unexpected<status_code>{status_code::error};
    auto point = detail::p256_point_bytes(key);
    if (!point)
        return unexpected<status_code>{point.error()};

    EVP_PKEY *raw = nullptr;
#if OPENSSL_VERSION_NUMBER >= 0x30000000L && !defined(LIBRESSL_VERSION_NUMBER)
    detail::ossl_ptr<EVP_PKEY_CTX, EVP_PKEY_CTX_free> ctx{EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr), EVP_PKEY_CTX_free};
    char                                              group[] = "prime256v1";
    OSSL_PARAM params[] = {OSSL_PARAM_construct_utf8_string(OSSL_PKEY_PARAM_GROUP_NAME, group, 0),
                           OSSL_PARAM_construct_octet_string(OSSL_PKEY_PARAM_PUB_KEY, point->data(), point->size()),
                           OSSL_PARAM_construct_end()};
    if (!ctx || EVP_PKEY_fromdata_init(ctx.get()) != 1 || EVP_PKEY_fromdata(ctx.get(), &raw, EVP_PKEY_PUBLIC_KEY, params) != 1) {
        EVP_PKEY_free(raw);
        return unexpected<status_code>{status_code::error};
    }
#else
    detail::ossl_ptr<EC_KEY, EC_KEY_free> ec{EC_KEY_new_by_curve_name(NID_X9_62_prime256v1), EC_KEY_free};
    const unsigned char                  *bytes  = point->data();
    auto                                 *ec_raw = ec.release();
    if (!ec_raw || !o2i_ECPublicKey(&ec_raw, &bytes, static_cast<long>(point->size()))) {
        EC_KEY_free(ec_raw);
        return unexpected<status_code>{status_code::error};
    }
    ec.reset(ec_raw);
    raw = EVP_PKEY_new();
    if (!raw || EVP_PKEY_set1_EC_KEY(raw, ec.get()) != 1) {
        EVP_PKEY_free(raw);
        return unexpected<status_code>{status_code::error};
    }
#endif
    return openssl_verification_key{raw, alg};
}
} // namespace cbor::tags::cwt
