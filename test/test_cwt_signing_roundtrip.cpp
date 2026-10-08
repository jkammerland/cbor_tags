#include "cwt_signing_test_support.h"

using namespace cwt_test;

TEST_SUITE("roundtrip/cwt") {

    TEST_CASE("cose_sign1 replacement clears prior verification state") {
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
        REQUIRE(make_decoder<codec::cwt>(*encoded)(decoded));

        CHECK(decoded.protected_header.empty());
        CHECK(decoded.unprotected.empty());
        CHECK_EQ(decoded.payload, source.payload);
        CHECK(decoded.signature.empty());
        CHECK_FALSE(verify_sign1<signature_presence_es256_backend>(nullptr, decoded));
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

} // TEST_SUITE("roundtrip/cwt")
