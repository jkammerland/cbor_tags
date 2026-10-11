#include "cwt_openssl_test_support.h"
#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
#include <cbor_tags/cwt/openssl_keys.h>

using namespace cbor::tags;

TEST_SUITE_BEGIN("roundtrip/cwt/keys");

TEST_CASE("cwt imported key verifies real signatures and retains algorithm restriction") {
    auto signing_key = cwt_test::make_p256_key();
    REQUIRE(signing_key);

    std::array<unsigned char, 65> point{};
#if OPENSSL_VERSION_NUMBER >= 0x30000000L && !defined(LIBRESSL_VERSION_NUMBER)
    std::size_t length{};
    REQUIRE(EVP_PKEY_get_octet_string_param(signing_key.get(), OSSL_PKEY_PARAM_PUB_KEY, point.data(), point.size(), &length) == 1);
    REQUIRE(length == point.size());
#else
    const auto *ec = EVP_PKEY_get0_EC_KEY(signing_key.get());
    REQUIRE(ec);
    REQUIRE(EC_POINT_point2oct(EC_KEY_get0_group(ec), EC_KEY_get0_public_key(ec), POINT_CONVERSION_UNCOMPRESSED, point.data(), point.size(),
                               nullptr) == point.size());
#endif

    cwt::ec2_public_key public_key;
    public_key.alg     = cwt::algorithm::esp256;
    public_key.key_ops = std::vector<std::int64_t>{2};
    public_key.x.assign(reinterpret_cast<const std::byte *>(point.data() + 1), reinterpret_cast<const std::byte *>(point.data() + 33));
    public_key.y  = std::vector<std::byte>{reinterpret_cast<const std::byte *>(point.data() + 33),
                                           reinterpret_cast<const std::byte *>(point.data() + 65)};
    auto imported = cwt::import_p256_public_key(public_key);
    REQUIRE(imported);

    std::vector<std::byte> payload{std::byte{1}};
    std::vector<std::byte> aad{std::byte{2}};
    auto                   signed_message = cwt::sign1<cwt::openssl_esp256_backend>(signing_key.get(), {}, {}, payload, aad);
    REQUIRE(signed_message);
    CHECK(cwt::verify_sign1<cwt::openssl_esp256_backend>(*imported, *signed_message, aad));
    aad.front() = std::byte{3};
    CHECK_FALSE(cwt::verify_sign1<cwt::openssl_esp256_backend>(*imported, *signed_message, aad));

    std::array<std::span<const std::byte>, 1> parts{std::span<const std::byte>{payload}};
    auto                                      signature = cwt::openssl_esp256_backend::sign(signing_key.get(), parts);
    REQUIRE(signature);
    CHECK(cwt::openssl_esp256_backend::verify(*imported, parts, *signature));
    CHECK_FALSE(cwt::openssl_es256_verifier::verify(*imported, parts, *signature));
}
TEST_SUITE_END();
#endif
