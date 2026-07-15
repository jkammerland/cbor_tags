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

evp_pkey_ptr make_p256_key() {
    evp_pkey_ctx_ptr context{EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr)};
    REQUIRE(context);
    REQUIRE(EVP_PKEY_keygen_init(context.get()) == 1);
    REQUIRE(EVP_PKEY_CTX_set_ec_paramgen_curve_nid(context.get(), NID_X9_62_prime256v1) == 1);

    EVP_PKEY *raw_key{};
    REQUIRE(EVP_PKEY_keygen(context.get(), &raw_key) == 1);
    return evp_pkey_ptr{raw_key};
}
#endif

struct toy_es256_backend {
    static constexpr algorithm                algorithm_id = algorithm::es256;
    static expected<byte_string, status_code> sign(void *, std::span<const std::byte>) { return byte_string{}; }
    static expected<void, status_code>        verify(void *, std::span<const std::byte>, std::span<const std::byte>) { return {}; }
};

} // namespace

TEST_CASE("CWT claims encode with registered integer claim keys") {
    claims_set claims{
        .issuer     = "coap://as.example.com",
        .subject    = "erikw",
        .audience   = "coap://light.example.com",
        .expiration = std::int64_t{1444064944},
        .not_before = std::int64_t{1443944944},
        .issued_at  = std::int64_t{1443944944},
        .cwt_id     = byte_string{std::byte{0x0b}, std::byte{0x71}},
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

TEST_CASE("encoded item view options decode non-empty CWT claims maps") {
    claims_set claims;
    claims.issuer     = "idp";
    claims.expiration = std::int64_t{42};

    std::vector<std::byte> encoded;
    auto                   enc = make_encoder(encoded);
    REQUIRE(enc(claims));

    claims_set decoded;
    auto       dec    = make_decoder_with_options<encoded_item_view_decoder_options>(encoded);
    auto       result = dec(decoded);

    REQUIRE(result);
    CHECK_EQ(decoded.issuer, claims.issuer);
    CHECK_EQ(decoded.expiration, claims.expiration);
    CHECK_EQ(to_hex(result->bytes()), to_hex(encoded));
    CHECK_EQ(dec.tell(), encoded.end());
}

TEST_CASE("COSE protected header and Sign1 Sig_structure encode in RFC shape") {
    const auto protected_header = encode_protected_header(header_map{.alg = algorithm::es256});
    REQUIRE(protected_header);
    CHECK_EQ(to_hex(*protected_header), "a10126");

    cose_sign1 message{
        .protected_header = *protected_header,
        .unprotected      = {},
        .payload          = byte_string{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}},
        .signature        = byte_string(64, std::byte{0xA5}),
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

TEST_CASE("COSE Sign Sig_structure uses body and signature protected headers") {
    const auto body_protected = encode_protected_header(header_map{.alg = algorithm::es256});
    REQUIRE(body_protected);
    const auto signature_protected = encode_protected_header(header_map{.kid = byte_string{std::byte{0x01}}});
    REQUIRE(signature_protected);

    cose_sign message{
        .protected_header = *body_protected,
        .unprotected      = {},
        .payload          = byte_string{std::byte{0x01}, std::byte{0x02}},
        .signatures       = {},
    };
    cose_signature signature{
        .protected_header = *signature_protected,
        .unprotected      = {},
        .signature        = byte_string(64, std::byte{0xA5}),
    };
    message.signatures.push_back(signature);

    const auto to_be_signed = make_sign_tbs(message, signature);
    REQUIRE(to_be_signed);
    CHECK_EQ(to_hex(*to_be_signed), "85695369676e617475726543a1012644a104410140420102");

    std::vector<std::byte> encoded;
    auto                   enc = make_encoder(encoded);
    REQUIRE(enc(as_cwt(as_cose_sign(message))));

    CHECK(encoded.size() > signature.signature.size());
    CHECK_EQ(to_hex(std::span<const std::byte>{encoded.data(), 9}), "d83dd8628443a10126");
}

TEST_CASE("COSE signing structs decode from arrays and CWT tagged wrappers") {
    const auto sign1_protected = encode_protected_header(header_map{.alg = algorithm::es256});
    REQUIRE(sign1_protected);
    cose_sign1 sign1_message{
        .protected_header = *sign1_protected,
        .unprotected      = header_map{.kid = byte_string{std::byte{0x01}}},
        .payload          = byte_string{std::byte{0x01}, std::byte{0x02}},
        .signature        = byte_string(64, std::byte{0xA5}),
    };

    std::vector<std::byte> encoded_sign1;
    auto                   sign1_enc = make_encoder(encoded_sign1);
    REQUIRE(sign1_enc(as_cwt(as_cose_sign1(sign1_message))));

    cose_sign1 decoded_sign1;
    auto       sign1_decoded_tag = make_tag_pair(cose_sign1_tag{}, decoded_sign1);
    auto       sign1_decoded_cwt = make_tag_pair(cwt_tag{}, sign1_decoded_tag);
    auto       sign1_dec         = make_decoder(encoded_sign1);
    REQUIRE(sign1_dec(sign1_decoded_cwt));
    CHECK_EQ(decoded_sign1.protected_header, sign1_message.protected_header);
    CHECK_EQ(decoded_sign1.unprotected.kid, sign1_message.unprotected.kid);
    CHECK_EQ(decoded_sign1.payload, sign1_message.payload);
    CHECK_EQ(decoded_sign1.signature, sign1_message.signature);

    const auto signature_protected = encode_protected_header(header_map{.kid = byte_string{std::byte{0x02}}});
    REQUIRE(signature_protected);
    cose_signature signature{
        .protected_header = *signature_protected,
        .unprotected      = {},
        .signature        = byte_string(64, std::byte{0x5A}),
    };

    std::vector<std::byte> encoded_signature;
    auto                   signature_enc = make_encoder(encoded_signature);
    REQUIRE(signature_enc(signature));

    cose_signature decoded_signature;
    auto           signature_dec = make_decoder(encoded_signature);
    REQUIRE(signature_dec(decoded_signature));
    CHECK_EQ(decoded_signature.protected_header, signature.protected_header);
    CHECK_EQ(decoded_signature.unprotected.kid, signature.unprotected.kid);
    CHECK_EQ(decoded_signature.signature, signature.signature);

    cose_sign sign_message{
        .protected_header = *sign1_protected,
        .unprotected      = {},
        .payload          = byte_string{std::byte{0x03}, std::byte{0x04}},
        .signatures       = {signature},
    };

    std::vector<std::byte> encoded_sign;
    auto                   sign_enc = make_encoder(encoded_sign);
    REQUIRE(sign_enc(as_cwt(as_cose_sign(sign_message))));

    cose_sign decoded_sign;
    auto      sign_decoded_tag = make_tag_pair(cose_sign_tag{}, decoded_sign);
    auto      sign_decoded_cwt = make_tag_pair(cwt_tag{}, sign_decoded_tag);
    auto      sign_dec         = make_decoder(encoded_sign);
    REQUIRE(sign_dec(sign_decoded_cwt));
    CHECK_EQ(decoded_sign.protected_header, sign_message.protected_header);
    CHECK_EQ(decoded_sign.payload, sign_message.payload);
    REQUIRE_EQ(decoded_sign.signatures.size(), 1U);
    CHECK_EQ(decoded_sign.signatures.front().protected_header, signature.protected_header);
    CHECK_EQ(decoded_sign.signatures.front().signature, signature.signature);
}

TEST_CASE("COSE Sign1 validates protected header algorithm for backend") {
    const auto protected_header = encode_protected_header(header_map{.alg = algorithm::es384});
    REQUIRE(protected_header);

    cose_sign1 message{
        .protected_header = *protected_header,
        .unprotected      = {},
        .payload          = byte_string{std::byte{0x01}},
        .signature        = byte_string(64, std::byte{0x00}),
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
        .unprotected      = header_map{.alg = algorithm::es256},
        .payload          = payload,
        .signature        = byte_string(64, std::byte{0x00}),
    };

    auto verify_unprotected_alg = verify_sign1<toy_es256_backend>(nullptr, message);
    REQUIRE_FALSE(verify_unprotected_alg);
    CHECK_EQ(verify_unprotected_alg.error(), status_code::error);
}

TEST_CASE("COSE Sign helpers create and validate signature entries") {
    const byte_string payload{std::byte{0x01}, std::byte{0x02}};

    auto message = sign<toy_es256_backend>(nullptr, {}, {}, payload, header_map{.kid = byte_string{std::byte{0x01}}});
    REQUIRE(message);
    REQUIRE_EQ(message->signatures.size(), 1U);
    CHECK(message->protected_header.empty());

    auto signature_header = decode_protected_header(message->signatures.front().protected_header);
    REQUIRE(signature_header);
    CHECK_EQ(signature_header->alg, algorithm::es256);
    CHECK_EQ(signature_header->kid, byte_string{std::byte{0x01}});

    REQUIRE(verify_sign<toy_es256_backend>(nullptr, *message));
    REQUIRE(verify_sign<toy_es256_backend>(nullptr, *message, std::size_t{0}));

    auto missing_signature = verify_sign<toy_es256_backend>(nullptr, *message, std::size_t{1});
    REQUIRE_FALSE(missing_signature);
    CHECK_EQ(missing_signature.error(), status_code::unexpected_group_size);
}

TEST_CASE("COSE Sign helpers append and validate multiple signature entries") {
    const byte_string payload{std::byte{0x01}, std::byte{0x02}};

    auto message = sign<toy_es256_backend>(nullptr, {}, {}, payload, header_map{.kid = byte_string{std::byte{0x01}}});
    REQUIRE(message);

    auto appended = add_signature<toy_es256_backend>(nullptr, *message, header_map{.kid = byte_string{std::byte{0x02}}}, {});
    REQUIRE(appended);
    REQUIRE_EQ(message->signatures.size(), 2U);

    auto first_header = decode_protected_header(message->signatures[0].protected_header);
    REQUIRE(first_header);
    CHECK_EQ(first_header->alg, algorithm::es256);
    CHECK_EQ(first_header->kid, byte_string{std::byte{0x01}});

    auto second_header = decode_protected_header(message->signatures[1].protected_header);
    REQUIRE(second_header);
    CHECK_EQ(second_header->alg, algorithm::es256);
    CHECK_EQ(second_header->kid, byte_string{std::byte{0x02}});

    REQUIRE(verify_sign<toy_es256_backend>(nullptr, *message));
    REQUIRE(verify_sign<toy_es256_backend>(nullptr, *message, std::size_t{0}));
    REQUIRE(verify_sign<toy_es256_backend>(nullptr, *message, std::size_t{1}));
}

TEST_CASE("COSE Sign rejects conflicting or unprotected algorithm headers") {
    const byte_string payload{std::byte{0x01}};

    auto sign_body_conflict = sign<toy_es256_backend>(nullptr, header_map{.alg = algorithm::es384}, {}, payload);
    REQUIRE_FALSE(sign_body_conflict);
    CHECK_EQ(sign_body_conflict.error(), status_code::error);

    auto sign_body_unprotected_alg = sign<toy_es256_backend>(nullptr, {}, header_map{.alg = algorithm::es256}, payload);
    REQUIRE_FALSE(sign_body_unprotected_alg);
    CHECK_EQ(sign_body_unprotected_alg.error(), status_code::error);

    auto sign_signature_unprotected_alg = sign<toy_es256_backend>(nullptr, {}, {}, payload, {}, header_map{.alg = algorithm::es256});
    REQUIRE_FALSE(sign_signature_unprotected_alg);
    CHECK_EQ(sign_signature_unprotected_alg.error(), status_code::error);

    const auto body_protected = encode_protected_header(header_map{.alg = algorithm::es256});
    REQUIRE(body_protected);

    cose_sign message{
        .protected_header = *body_protected,
        .unprotected      = {},
        .payload          = payload,
        .signatures       = {cose_signature{
                  .protected_header = {},
                  .unprotected      = header_map{.alg = algorithm::es256},
                  .signature        = byte_string(64, std::byte{0x00}),
        }},
    };

    auto verify_signature_unprotected_alg = verify_sign<toy_es256_backend>(nullptr, message);
    REQUIRE_FALSE(verify_signature_unprotected_alg);
    CHECK_EQ(verify_signature_unprotected_alg.error(), status_code::error);
}

#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
TEST_CASE("CWT crypto backend signs and verifies COSE Sign1 ES256") {
    auto key = make_p256_key();

    const byte_string payload{std::byte{0xA1}, std::byte{0x01}, std::byte{0x02}};
    auto message = sign1<crypto_es256_backend>(key.get(), header_map{.kid = byte_string{std::byte{0x01}, std::byte{0x02}}}, {}, payload);

    REQUIRE(message);
    CHECK_EQ(message->signature.size(), 64U);
    REQUIRE(verify_sign1<crypto_es256_backend>(key.get(), *message));

    message->payload->front() = std::byte{0xA2};
    auto verify_tampered      = verify_sign1<crypto_es256_backend>(key.get(), *message);
    REQUIRE_FALSE(verify_tampered);
    CHECK_EQ(verify_tampered.error(), status_code::error);
}

TEST_CASE("CWT crypto backend signs and verifies COSE Sign ES256") {
    auto key = make_p256_key();

    const byte_string payload{std::byte{0xA1}, std::byte{0x01}, std::byte{0x02}};
    auto message = sign<crypto_es256_backend>(key.get(), {}, {}, payload, header_map{.kid = byte_string{std::byte{0x01}, std::byte{0x02}}});

    REQUIRE(message);
    REQUIRE_EQ(message->signatures.size(), 1U);
    CHECK_EQ(message->signatures.front().signature.size(), 64U);
    REQUIRE(verify_sign<crypto_es256_backend>(key.get(), *message));

    message->payload->front() = std::byte{0xA2};
    auto verify_tampered      = verify_sign<crypto_es256_backend>(key.get(), *message);
    REQUIRE_FALSE(verify_tampered);
    CHECK_EQ(verify_tampered.error(), status_code::error);
}
#endif
