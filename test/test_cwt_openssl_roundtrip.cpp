#include "cwt_openssl_test_support.h"

using namespace cwt_test;

TEST_SUITE("roundtrip/cwt") {

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

#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
    TEST_CASE("real es256 verification accepts decoded sign1 and sign views") {
        auto              key = make_p256_key();
        const byte_string payload{std::byte{1}, std::byte{2}, std::byte{3}};
        const byte_string aad{std::byte{4}};
        auto              single = sign1<crypto_es256_backend>(key.get(), {}, {}, payload, aad);
        REQUIRE(single);
        auto encoded = encode_to_bytes(*single);
        REQUIRE(encoded);
        cose_sign1_view view;
        REQUIRE(make_decoder<codec::cwt>(*encoded)(view));
        REQUIRE(verify_sign1<crypto_es256_backend>(key.get(), view, aad));
        REQUIRE(view.payload);
        const auto offset = static_cast<std::size_t>(view.payload->data() - encoded->data());
        (*encoded)[offset] ^= std::byte{1};
        CHECK_FALSE(verify_sign1<crypto_es256_backend>(key.get(), view, aad));

        auto multiple = sign<crypto_es256_backend>(key.get(), {}, {}, payload, {}, {}, aad);
        REQUIRE(multiple);
        REQUIRE(add_signature<crypto_es256_backend>(key.get(), *multiple, {}, {}, aad));
        const auto multi_bytes = encode_to_bytes(*multiple);
        REQUIRE(multi_bytes);
        cose_sign_view multi_view;
        REQUIRE(make_decoder<codec::cwt>(*multi_bytes)(multi_view));
        REQUIRE(multi_view.signatures.size() == 2);
        REQUIRE(verify_sign<crypto_es256_backend>(key.get(), multi_view, aad));
        REQUIRE(verify_sign<crypto_es256_backend>(key.get(), multi_view, std::size_t{1}, aad));
        REQUIRE(verify_signature<crypto_es256_backend>(key.get(), multi_view, multiple->signatures[0], aad));
        multi_view.payload.reset();
        CHECK_FALSE(verify_sign<crypto_es256_backend>(key.get(), multi_view, aad));
        REQUIRE(verify_sign<crypto_es256_backend>(key.get(), multi_view, aad, byte_view{payload}));
        view.payload.reset();
        REQUIRE(verify_sign1<crypto_es256_backend>(key.get(), view, aad, byte_view{payload}));
    }
#endif

} // TEST_SUITE("roundtrip/cwt")
