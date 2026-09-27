#include "cwt_test_support.h"

using namespace cwt_test;

TEST_SUITE("roundtrip/cwt") {

    TEST_CASE("CWT registered claims roundtrip typed values") {
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

    TEST_CASE("CWT NumericDate encoding rejects non-finite typed values") {
        for (const auto value : {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
                                 std::numeric_limits<double>::quiet_NaN()}) {
            claims_set claims;
            claims.expiration = value;

            std::vector<std::byte> output;
            auto                   result = make_encoder(output)(claims);
            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), status_code::error);
            CHECK(output.empty());
        }
    }

    TEST_CASE("COSE typed header rejects an absent critical target") {
        SUBCASE("typed header omits a critical target") {
            auto encoded = encode_protected_header(header_map{.alg = std::nullopt, .kid = std::nullopt, .crit = {integer{1}}});
            REQUIRE_FALSE(encoded);
            CHECK_EQ(encoded.error(), status_code::error);
        }
    }

    TEST_CASE("COSE decoders atomically replace signing state") {
        SUBCASE("cose_signature and cose_sign replace preseeded containers") {
            const cose_signature signature_source{};
            auto                 signature_bytes = encode_to_bytes(signature_source);
            REQUIRE(signature_bytes);

            cose_signature decoded_signature{
                .protected_header = byte_string{std::byte{0x10}},
                .unprotected      = header_map{.alg = std::nullopt, .kid = byte_string{std::byte{0x11}}, .crit = {}},
                .signature        = byte_string{std::byte{0x12}},
            };
            REQUIRE(make_decoder(*signature_bytes)(decoded_signature));
            CHECK(decoded_signature.protected_header.empty());
            CHECK(decoded_signature.unprotected.empty());
            CHECK(decoded_signature.signature.empty());

            const cose_sign source{
                .protected_header = {},
                .unprotected      = {},
                .payload          = byte_string{std::byte{0x21}},
                .signatures       = {signature_source},
            };
            auto sign_bytes = encode_to_bytes(source);
            REQUIRE(sign_bytes);

            cose_sign decoded_sign{
                .protected_header = byte_string{std::byte{0x22}},
                .unprotected      = header_map{.alg = std::nullopt, .kid = byte_string{std::byte{0x23}}, .crit = {}},
                .payload          = byte_string{std::byte{0x24}},
                .signatures       = {cose_signature{
                    .protected_header = byte_string{std::byte{0x25}},
                    .unprotected      = header_map{.alg = std::nullopt, .kid = byte_string{std::byte{0x26}}, .crit = {}},
                    .signature        = byte_string{std::byte{0x27}},
                }},
            };
            REQUIRE(make_decoder(*sign_bytes)(decoded_sign));
            CHECK(decoded_sign.protected_header.empty());
            CHECK(decoded_sign.unprotected.empty());
            CHECK_EQ(decoded_sign.payload, source.payload);
            REQUIRE_EQ(decoded_sign.signatures.size(), 1U);
            CHECK(decoded_sign.signatures.front().protected_header.empty());
            CHECK(decoded_sign.signatures.front().unprotected.empty());
            CHECK(decoded_sign.signatures.front().signature.empty());
        }

        SUBCASE("cose_sign1 clears authentication bytes absent from the new message") {
            const byte_string payload{std::byte{0x31}};
            auto              seeded = sign1<signature_presence_es256_backend>(nullptr, {}, {}, payload);
            REQUIRE(seeded);

            cose_sign1 decoded      = std::move(*seeded);
            decoded.unprotected.kid = byte_string{std::byte{0x32}};
            REQUIRE(verify_sign1<signature_presence_es256_backend>(nullptr, decoded));

            const cose_sign1 source{
                .protected_header = {},
                .unprotected      = {},
                .payload          = payload,
                .signature        = {},
            };
            auto encoded = encode_to_bytes(source);
            REQUIRE(encoded);
            REQUIRE(make_decoder(*encoded)(decoded));

            CHECK(decoded.protected_header.empty());
            CHECK(decoded.unprotected.empty());
            CHECK_EQ(decoded.payload, source.payload);
            CHECK(decoded.signature.empty());
            CHECK_FALSE(verify_sign1<signature_presence_es256_backend>(nullptr, decoded));
        }

        SUBCASE("sig_structure switches cleanly between four and five element forms") {
            const sig_structure five{
                .context        = "Signature",
                .body_protected = byte_string{std::byte{0x61}},
                .sign_protected = byte_string{std::byte{0x62}},
                .external_aad   = byte_string{std::byte{0x63}},
                .payload        = byte_string{std::byte{0x64}},
            };
            auto five_bytes = encode_to_bytes(five);
            REQUIRE(five_bytes);

            sig_structure decoded{
                .context        = "old",
                .body_protected = byte_string{std::byte{0x71}},
                .sign_protected = byte_string{std::byte{0x72}},
                .external_aad   = byte_string{std::byte{0x73}},
                .payload        = byte_string{std::byte{0x74}},
            };
            REQUIRE(make_decoder(*five_bytes)(decoded));
            CHECK_EQ(decoded.context, five.context);
            CHECK_EQ(decoded.body_protected, five.body_protected);
            CHECK_EQ(decoded.sign_protected, five.sign_protected);
            CHECK_EQ(decoded.external_aad, five.external_aad);
            CHECK_EQ(decoded.payload, five.payload);

            const sig_structure four{
                .context        = "Signature1",
                .body_protected = byte_string{std::byte{0x81}},
                .sign_protected = std::nullopt,
                .external_aad   = byte_string{std::byte{0x82}},
                .payload        = byte_string{std::byte{0x83}},
            };
            auto four_bytes = encode_to_bytes(four);
            REQUIRE(four_bytes);
            REQUIRE(make_decoder(*four_bytes)(decoded));
            CHECK_EQ(decoded.context, four.context);
            CHECK_EQ(decoded.body_protected, four.body_protected);
            CHECK_FALSE(decoded.sign_protected);
            CHECK_EQ(decoded.external_aad, four.external_aad);
            CHECK_EQ(decoded.payload, four.payload);
        }
    }

    TEST_CASE("COSE Sign encoding requires at least one signature") {
        cose_sign empty{
            .protected_header = {},
            .unprotected      = {},
            .payload          = byte_string{std::byte{0x01}},
            .signatures       = {},
        };
        std::vector<std::byte> encoded;
        auto                   encode_result = make_encoder(encoded)(empty);
        REQUIRE_FALSE(encode_result);
        CHECK_EQ(encode_result.error(), status_code::unexpected_group_size);
        CHECK(encoded.empty());
    }

    TEST_CASE("COSE protected headers roundtrip supported critical labels") {
        header_map input{
            .alg  = algorithm::es256,
            .kid  = byte_string{std::byte{0x01}},
            .crit = {integer{1}, integer{4}},
        };

        auto encoded = encode_protected_header(input);
        REQUIRE(encoded);

        auto decoded = decode_protected_header(*encoded);
        REQUIRE(decoded);
        CHECK_EQ(decoded->alg, input.alg);
        CHECK_EQ(decoded->kid, input.kid);
        CHECK(decoded->crit == input.crit);
    }

    TEST_CASE("COSE detached payload APIs distinguish empty from missing") {
        cose_sign1 sign1_message{
            .protected_header = {},
            .unprotected      = {},
            .payload          = std::nullopt,
            .signature        = {},
        };

        auto missing_sign1 = make_sign1_sig_structure(sign1_message);
        REQUIRE_FALSE(missing_sign1);
        CHECK_EQ(missing_sign1.error(), status_code::error);

        const auto empty_payload = std::span<const std::byte>{};
        auto       empty_sign1   = make_sign1_sig_structure(sign1_message, {}, empty_payload);
        REQUIRE(empty_sign1);
        CHECK(empty_sign1->payload.empty());
        REQUIRE(verify_sign1<toy_es256_backend>(nullptr, sign1_message, {}, empty_payload));

        const byte_string embedded{std::byte{0x01}};
        const byte_string detached{std::byte{0x02}};
        sign1_message.payload = embedded;
        auto embedded_sign1   = make_sign1_sig_structure(sign1_message, {}, std::span<const std::byte>{detached.data(), detached.size()});
        REQUIRE(embedded_sign1);
        CHECK_EQ(embedded_sign1->payload, embedded);

        cose_signature signature{};
        cose_sign      sign_message{
            .protected_header = {},
            .unprotected      = {},
            .payload          = std::nullopt,
            .signatures       = {signature},
        };

        auto missing_sign = make_sign_sig_structure(sign_message, signature);
        REQUIRE_FALSE(missing_sign);
        CHECK_EQ(missing_sign.error(), status_code::error);

        auto empty_sign = make_sign_sig_structure(sign_message, signature, {}, empty_payload);
        REQUIRE(empty_sign);
        CHECK(empty_sign->payload.empty());
        REQUIRE(verify_sign<toy_es256_backend>(nullptr, sign_message, std::span<const std::byte>{}, empty_payload));
    }

    TEST_CASE("COSE signing structs decode from arrays and CWT tagged wrappers") {
        const auto sign1_protected = encode_protected_header(header_map{.alg = algorithm::es256, .kid = std::nullopt, .crit = {}});
        REQUIRE(sign1_protected);
        cose_sign1 sign1_message{
            .protected_header = *sign1_protected,
            .unprotected      = header_map{.alg = std::nullopt, .kid = byte_string{std::byte{0x01}}, .crit = {}},
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

        const auto signature_protected =
            encode_protected_header(header_map{.alg = std::nullopt, .kid = byte_string{std::byte{0x02}}, .crit = {}});
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
        const auto protected_header = encode_protected_header(header_map{.alg = algorithm::es384, .kid = std::nullopt, .crit = {}});
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

        auto sign_conflict =
            sign1<toy_es256_backend>(nullptr, header_map{.alg = algorithm::es384, .kid = std::nullopt, .crit = {}}, {}, payload);
        REQUIRE_FALSE(sign_conflict);
        CHECK_EQ(sign_conflict.error(), status_code::error);

        auto sign_unprotected_alg =
            sign1<toy_es256_backend>(nullptr, {}, header_map{.alg = algorithm::es256, .kid = std::nullopt, .crit = {}}, payload);
        REQUIRE_FALSE(sign_unprotected_alg);
        CHECK_EQ(sign_unprotected_alg.error(), status_code::error);

        const auto protected_header = encode_protected_header(header_map{.alg = algorithm::es256, .kid = std::nullopt, .crit = {}});
        REQUIRE(protected_header);

        cose_sign1 message{
            .protected_header = *protected_header,
            .unprotected      = header_map{.alg = algorithm::es256, .kid = std::nullopt, .crit = {}},
            .payload          = payload,
            .signature        = byte_string(64, std::byte{0x00}),
        };

        auto verify_unprotected_alg = verify_sign1<toy_es256_backend>(nullptr, message);
        REQUIRE_FALSE(verify_unprotected_alg);
        CHECK_EQ(verify_unprotected_alg.error(), status_code::error);
    }

    TEST_CASE("COSE signing rejects critical labels in unprotected headers") {
        const byte_string payload{std::byte{0x01}};
        const header_map  unprotected_critical{.alg = std::nullopt, .kid = byte_string{std::byte{0x01}}, .crit = {integer{4}}};

        auto sign1_result = sign1<toy_es256_backend>(nullptr, {}, unprotected_critical, payload);
        REQUIRE_FALSE(sign1_result);
        CHECK_EQ(sign1_result.error(), status_code::error);

        auto sign_body_result = sign<toy_es256_backend>(nullptr, {}, unprotected_critical, payload);
        REQUIRE_FALSE(sign_body_result);
        CHECK_EQ(sign_body_result.error(), status_code::error);

        auto sign_signature_result = sign<toy_es256_backend>(nullptr, {}, {}, payload, {}, unprotected_critical);
        REQUIRE_FALSE(sign_signature_result);
        CHECK_EQ(sign_signature_result.error(), status_code::error);

        auto protected_header = encode_protected_header(header_map{.alg = algorithm::es256, .kid = std::nullopt, .crit = {}});
        REQUIRE(protected_header);
        cose_sign1 message{
            .protected_header = std::move(*protected_header),
            .unprotected      = unprotected_critical,
            .payload          = payload,
            .signature        = {},
        };

        auto verify_result = verify_sign1<toy_es256_backend>(nullptr, message);
        REQUIRE_FALSE(verify_result);
        CHECK_EQ(verify_result.error(), status_code::error);
    }

    TEST_CASE("COSE Sign helpers create and validate signature entries") {
        const byte_string payload{std::byte{0x01}, std::byte{0x02}};

        auto message = sign<toy_es256_backend>(nullptr, {}, {}, payload,
                                               header_map{.alg = std::nullopt, .kid = byte_string{std::byte{0x01}}, .crit = {}});
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

        auto message = sign<toy_es256_backend>(nullptr, {}, {}, payload,
                                               header_map{.alg = std::nullopt, .kid = byte_string{std::byte{0x01}}, .crit = {}});
        REQUIRE(message);

        auto appended = add_signature<toy_es256_backend>(
            nullptr, *message, header_map{.alg = std::nullopt, .kid = byte_string{std::byte{0x02}}, .crit = {}}, {});
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

        auto sign_body_conflict =
            sign<toy_es256_backend>(nullptr, header_map{.alg = algorithm::es384, .kid = std::nullopt, .crit = {}}, {}, payload);
        REQUIRE_FALSE(sign_body_conflict);
        CHECK_EQ(sign_body_conflict.error(), status_code::error);

        auto sign_body_unprotected_alg =
            sign<toy_es256_backend>(nullptr, {}, header_map{.alg = algorithm::es256, .kid = std::nullopt, .crit = {}}, payload);
        REQUIRE_FALSE(sign_body_unprotected_alg);
        CHECK_EQ(sign_body_unprotected_alg.error(), status_code::error);

        auto sign_signature_unprotected_alg =
            sign<toy_es256_backend>(nullptr, {}, {}, payload, {}, header_map{.alg = algorithm::es256, .kid = std::nullopt, .crit = {}});
        REQUIRE_FALSE(sign_signature_unprotected_alg);
        CHECK_EQ(sign_signature_unprotected_alg.error(), status_code::error);

        const auto body_protected = encode_protected_header(header_map{.alg = algorithm::es256, .kid = std::nullopt, .crit = {}});
        REQUIRE(body_protected);

        cose_sign message{
            .protected_header = *body_protected,
            .unprotected      = {},
            .payload          = payload,
            .signatures       = {cose_signature{
                .protected_header = {},
                .unprotected      = header_map{.alg = algorithm::es256, .kid = std::nullopt, .crit = {}},
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
        auto              message = sign1<crypto_es256_backend>(
            key.get(), header_map{.alg = std::nullopt, .kid = byte_string{std::byte{0x01}, std::byte{0x02}}, .crit = {}}, {}, payload);

        REQUIRE(message);
        CHECK_EQ(message->signature.size(), 64U);
        REQUIRE(verify_sign1<crypto_es256_backend>(key.get(), *message));

        message->payload->front() = std::byte{0xA2};
        auto verify_tampered      = verify_sign1<crypto_es256_backend>(key.get(), *message);
        REQUIRE_FALSE(verify_tampered);
        CHECK_EQ(verify_tampered.error(), status_code::error);
    }
#endif

#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
    TEST_CASE("CWT crypto backend signs and verifies COSE Sign ES256") {
        auto key = make_p256_key();

        const byte_string payload{std::byte{0xA1}, std::byte{0x01}, std::byte{0x02}};
        auto              message = sign<crypto_es256_backend>(
            key.get(), {}, {}, payload, header_map{.alg = std::nullopt, .kid = byte_string{std::byte{0x01}, std::byte{0x02}}, .crit = {}});

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

#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
    TEST_CASE("CWT OpenSSL ES256 backend rejects incompatible key types and curves") {
        auto p256      = make_p256_key();
        auto secp256k1 = make_ec_key(NID_secp256k1);
        auto rsa       = make_rsa_key();

        const byte_string payload{std::byte{0x01}};
        auto              message = sign1<crypto_es256_backend>(p256.get(), {}, {}, payload);
        REQUIRE(message);
        REQUIRE(verify_sign1<crypto_es256_backend>(p256.get(), *message));

        for (auto *incompatible_key : {secp256k1.get(), rsa.get()}) {
            auto sign_result = sign1<crypto_es256_backend>(incompatible_key, {}, {}, payload);
            REQUIRE_FALSE(sign_result);
            CHECK_EQ(sign_result.error(), status_code::error);

            auto verify_result = verify_sign1<crypto_es256_backend>(incompatible_key, *message);
            REQUIRE_FALSE(verify_result);
            CHECK_EQ(verify_result.error(), status_code::error);
        }
    }
#endif

    TEST_CASE("CWT claims roundtrip array-valued audience") {
        claims_set claims;
        claims.audience = std::vector<std::string>{"coap://light.example.com", "coap://sensor.example.com"};

        std::vector<std::byte> encoded;
        auto                   enc = make_encoder(encoded);
        REQUIRE(enc(claims));

        claims_set decoded;
        auto       dec = make_decoder(encoded);
        REQUIRE(dec(decoded));
        REQUIRE(decoded.audience);
        REQUIRE(std::holds_alternative<std::vector<std::string>>(*decoded.audience));
        CHECK_EQ(std::get<std::vector<std::string>>(*decoded.audience), std::get<std::vector<std::string>>(*claims.audience));
    }

    TEST_CASE("CWT claims roundtrip an empty audience array") {
        claims_set claims;
        claims.audience = std::vector<std::string>{};

        std::vector<std::byte> encoded;
        auto                   enc = make_encoder(encoded);
        REQUIRE(enc(claims));

        claims_set decoded;
        REQUIRE(make_decoder(encoded)(decoded));
        REQUIRE(decoded.audience);
        REQUIRE(std::holds_alternative<std::vector<std::string>>(*decoded.audience));
        CHECK(std::get<std::vector<std::string>>(*decoded.audience).empty());
    }

} // TEST_SUITE("roundtrip/cwt")
