#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cwt/cwt.h>

#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
#include <cbor_tags/cwt/openssl_crypto.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#endif

using namespace cbor::tags;
using namespace cbor::tags::cwt;

namespace {

#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
using evp_pkey_ptr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using evp_pkey_ctx_ptr = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;

evp_pkey_ptr make_p256_key() {
    evp_pkey_ctx_ptr context{EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr), EVP_PKEY_CTX_free};
    REQUIRE(context);
    REQUIRE(EVP_PKEY_keygen_init(context.get()) == 1);
    REQUIRE(EVP_PKEY_CTX_set_ec_paramgen_curve_nid(context.get(), NID_X9_62_prime256v1) == 1);

    EVP_PKEY *raw_key{};
    REQUIRE(EVP_PKEY_keygen(context.get(), &raw_key) == 1);
    return evp_pkey_ptr{raw_key, EVP_PKEY_free};
}
#endif

struct toy_es256_backend {
    static constexpr algorithm algorithm_id = algorithm::es256;
    static expected<byte_string, status_code> sign(void *, std::span<const std::byte>) { return byte_string{}; }
    static expected<void, status_code>        verify(void *, std::span<const std::byte>, std::span<const std::byte>) { return {}; }
};

} // namespace

TEST_CASE("CWT claims encode with registered integer claim keys") {
    claims_set claims{
        .issuer = "coap://as.example.com",
        .subject = "erikw",
        .audience = "coap://light.example.com",
        .expiration = std::int64_t{1444064944},
        .not_before = std::int64_t{1443944944},
        .issued_at = std::int64_t{1443944944},
        .cwt_id = byte_string{std::byte{0x0b}, std::byte{0x71}},
    };

    std::vector<std::byte> encoded;
    auto                   enc = make_encoder(encoded);
    REQUIRE(enc(claims));

    CHECK_EQ(to_hex(encoded),
             "a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b77037818636f61703a2f2f6c696768742e6578616d706c652e6"
             "36f6d041a5612aeb0051a5610d9f0061a5610d9f007420b71");

    claims_set decoded;
    auto       dec = make_decoder(encoded);
    REQUIRE(dec(decoded));
    CHECK_EQ(decoded.issuer, claims.issuer);
    CHECK_EQ(decoded.subject, claims.subject);
    CHECK_EQ(decoded.audience, claims.audience);
    CHECK_EQ(decoded.expiration, claims.expiration);
    CHECK_EQ(decoded.not_before, claims.not_before);
    CHECK_EQ(decoded.issued_at, claims.issued_at);
    CHECK_EQ(decoded.cwt_id, claims.cwt_id);
}

TEST_CASE("COSE protected header and Sign1 Sig_structure encode in RFC shape") {
    const auto protected_header = encode_protected_header(header_map{.alg = algorithm::es256});
    REQUIRE(protected_header);
    CHECK_EQ(to_hex(*protected_header), "a10126");

    cose_sign1 message{
        .protected_header = *protected_header,
        .unprotected = {},
        .payload = byte_string{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}},
        .signature = byte_string(64, std::byte{0xA5}),
    };

    const auto to_be_signed = make_sign1_tbs(message);
    REQUIRE(to_be_signed);
    CHECK_EQ(to_hex(*to_be_signed), "846a5369676e61747572653143a101264043010203");

    std::vector<std::byte> encoded;
    auto                   enc = make_encoder(encoded);
    REQUIRE(enc(as_cwt(as_cose_sign1(message))));

    CHECK(encoded.size() > message.signature.size());
    CHECK_EQ(to_hex(std::span<const std::byte>{encoded.data(), 8}), "d83dd28443a10126");
}

TEST_CASE("COSE Sign1 validates protected header algorithm for backend") {
    const auto protected_header = encode_protected_header(header_map{.alg = algorithm::es384});
    REQUIRE(protected_header);

    cose_sign1 message{
        .protected_header = *protected_header,
        .unprotected = {},
        .payload = byte_string{std::byte{0x01}},
        .signature = byte_string(64, std::byte{0x00}),
    };

    auto result = verify_sign1<toy_es256_backend>(nullptr, message);
    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::error);
}

TEST_CASE("COSE Sign1 rejects conflicting or unprotected algorithm headers") {
    const byte_string payload{std::byte{0x01}};

    auto sign_conflict = sign1<toy_es256_backend>(nullptr, header_map{.alg = algorithm::es384}, {}, payload);
    REQUIRE_FALSE(sign_conflict);
    CHECK_EQ(sign_conflict.error(), status_code::error);

    auto sign_unprotected_alg = sign1<toy_es256_backend>(nullptr, {}, header_map{.alg = algorithm::es256}, payload);
    REQUIRE_FALSE(sign_unprotected_alg);
    CHECK_EQ(sign_unprotected_alg.error(), status_code::error);

    const auto protected_header = encode_protected_header(header_map{.alg = algorithm::es256});
    REQUIRE(protected_header);

    cose_sign1 message{
        .protected_header = *protected_header,
        .unprotected = header_map{.alg = algorithm::es256},
        .payload = payload,
        .signature = byte_string(64, std::byte{0x00}),
    };

    auto verify_unprotected_alg = verify_sign1<toy_es256_backend>(nullptr, message);
    REQUIRE_FALSE(verify_unprotected_alg);
    CHECK_EQ(verify_unprotected_alg.error(), status_code::error);
}

#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
TEST_CASE("OpenSSL CWT backend signs and verifies COSE Sign1 ES256") {
    auto key = make_p256_key();

    const byte_string payload{std::byte{0xA1}, std::byte{0x01}, std::byte{0x02}};
    auto message =
        sign1<openssl_es256_backend>(key.get(), header_map{.kid = byte_string{std::byte{0x01}, std::byte{0x02}}}, {}, payload);

    REQUIRE(message);
    CHECK_EQ(message->signature.size(), 64U);
    REQUIRE(verify_sign1<openssl_es256_backend>(key.get(), *message));

    message->payload->front() = std::byte{0xA2};
    auto verify_tampered = verify_sign1<openssl_es256_backend>(key.get(), *message);
    REQUIRE_FALSE(verify_tampered);
    CHECK_EQ(verify_tampered.error(), status_code::error);
}
#endif
