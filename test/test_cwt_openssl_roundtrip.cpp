#include "cwt_openssl_test_support.h"

#include <array>
#include <sstream>

using namespace cwt_test;

TEST_SUITE("roundtrip/cwt") {

#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
    TEST_CASE("cryptographically valid algless Sign1 requires an explicit implicit profile") {
        auto       key = make_p256_key();
        cose_sign1 message{.protected_header = {},
                           .unprotected      = {},
                           .payload          = std::vector<std::byte>{std::byte{1}, std::byte{2}},
                           .signature        = {}};

        // Profiles that omit alg issue signatures through the low-level input/backend API.
        auto input = make_sign1_tbs(message);
        REQUIRE(input);
        auto signature = crypto_esp256_backend::sign(key.get(), cbor::tags::cwt::detail::signature_parts(*input));
        REQUIRE(signature);
        message.signature = std::move(*signature);
        REQUIRE(crypto_esp256_backend::verify(key.get(), cbor::tags::cwt::detail::signature_parts(*input), message.signature));
        CHECK_FALSE(verify_sign1<crypto_esp256_backend>(key.get(), message));
        REQUIRE(verify_sign1<crypto_esp256_backend>(key.get(), message, implicit_algorithm_profile));

        auto encoded = encode_to_bytes(message);
        REQUIRE(encoded);
        cose_sign1_view view;
        REQUIRE(make_decoder<codec::cwt>(*encoded)(view));
        CHECK_FALSE(verify_sign1<crypto_esp256_backend>(key.get(), view));
        REQUIRE(verify_sign1<crypto_esp256_backend>(key.get(), view, implicit_algorithm_profile));

        message.payload->front() ^= std::byte{1};
        CHECK_FALSE(verify_sign1<crypto_esp256_backend>(key.get(), message, implicit_algorithm_profile));
    }

    TEST_CASE("CWT crypto backend signs and verifies COSE Sign1 ESP256") {
        auto key = make_p256_key();

        const std::vector<std::byte> payload{std::byte{0xA1}, std::byte{0x01}, std::byte{0x02}};
        auto                         message = sign1<crypto_esp256_backend>(
            key.get(), header_map{.alg = std::nullopt, .kid = std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}}, .crit = {}}, {},
            payload);

        REQUIRE(message);
        CHECK_EQ(message->signature.size(), 64U);
        auto protected_header = decode_protected_header(message->protected_header);
        REQUIRE(protected_header);
        CHECK(protected_header->alg == algorithm::esp256);
        CHECK_FALSE(verify_sign1<openssl_es256_verifier>(key.get(), *message));
        REQUIRE(verify_sign1<crypto_esp256_backend>(key.get(), *message));

        message->payload->front() = std::byte{0xA2};
        auto verify_tampered      = verify_sign1<crypto_esp256_backend>(key.get(), *message);
        REQUIRE_FALSE(verify_tampered);
        CHECK_EQ(verify_tampered.error(), status_code::error);
    }
#endif

#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
    TEST_CASE("CWT crypto backend signs and verifies COSE Sign ESP256") {
        auto key = make_p256_key();

        const std::vector<std::byte> payload{std::byte{0xA1}, std::byte{0x01}, std::byte{0x02}};
        auto                         message = sign<crypto_esp256_backend>(
            key.get(), {}, {}, payload,
            header_map{.alg = std::nullopt, .kid = std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}}, .crit = {}});

        REQUIRE(message);
        REQUIRE_EQ(message->signatures.size(), 1U);
        CHECK_EQ(message->signatures.front().signature.size(), 64U);
        REQUIRE(verify_sign<crypto_esp256_backend>(key.get(), *message));

        message->payload->front() = std::byte{0xA2};
        auto verify_tampered      = verify_sign<crypto_esp256_backend>(key.get(), *message);
        REQUIRE_FALSE(verify_tampered);
        CHECK_EQ(verify_tampered.error(), status_code::error);
    }
#endif

#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
    TEST_CASE("CWT OpenSSL ESP256 backend rejects incompatible key types and curves") {
        auto p256      = make_p256_key();
        auto secp256k1 = make_ec_key(NID_secp256k1);
        auto rsa       = make_rsa_key();

        const std::vector<std::byte> payload{std::byte{0x01}};
        auto                         message = sign1<crypto_esp256_backend>(p256.get(), {}, {}, payload);
        REQUIRE(message);
        REQUIRE(verify_sign1<crypto_esp256_backend>(p256.get(), *message));

        for (auto *incompatible_key : {secp256k1.get(), rsa.get()}) {
            auto sign_result = sign1<crypto_esp256_backend>(incompatible_key, {}, {}, payload);
            REQUIRE_FALSE(sign_result);
            CHECK_EQ(sign_result.error(), status_code::error);

            auto verify_result = verify_sign1<crypto_esp256_backend>(incompatible_key, *message);
            REQUIRE_FALSE(verify_result);
            CHECK_EQ(verify_result.error(), status_code::error);
        }
    }
#endif

#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
    TEST_CASE("real esp256 verification accepts decoded sign1 and sign views") {
        auto                         key = make_p256_key();
        const std::vector<std::byte> payload{std::byte{1}, std::byte{2}, std::byte{3}};
        const std::vector<std::byte> aad{std::byte{4}};
        auto                         single = sign1<crypto_esp256_backend>(key.get(), {}, {}, payload, aad);
        REQUIRE(single);
        auto encoded = encode_to_bytes(*single);
        REQUIRE(encoded);
        cose_sign1_view view;
        REQUIRE(make_decoder<codec::cwt>(*encoded)(view));
        REQUIRE(verify_sign1<crypto_esp256_backend>(key.get(), view, aad));
        REQUIRE(view.payload);
        const auto offset = static_cast<std::size_t>(view.payload->data() - encoded->data());
        (*encoded)[offset] ^= std::byte{1};
        CHECK_FALSE(verify_sign1<crypto_esp256_backend>(key.get(), view, aad));

        auto multiple = sign<crypto_esp256_backend>(key.get(), {}, {}, payload, {}, {}, aad);
        REQUIRE(multiple);
        REQUIRE(add_signature<crypto_esp256_backend>(key.get(), *multiple, {}, {}, aad));
        const auto multi_bytes = encode_to_bytes(*multiple);
        REQUIRE(multi_bytes);
        cose_sign_view multi_view;
        REQUIRE(make_decoder<codec::cwt>(*multi_bytes)(multi_view));
        REQUIRE(multi_view.signatures.size() == 2);
        REQUIRE(verify_sign<crypto_esp256_backend>(key.get(), multi_view, aad));
        REQUIRE(verify_sign<crypto_esp256_backend>(key.get(), multi_view, std::size_t{1}, aad));
        REQUIRE(verify_signature<crypto_esp256_backend>(key.get(), multi_view, multiple->signatures[0], aad));
        multi_view.payload.reset();
        CHECK_FALSE(verify_sign<crypto_esp256_backend>(key.get(), multi_view, aad));
        REQUIRE(verify_sign<crypto_esp256_backend>(key.get(), multi_view, aad, std::span<const std::byte>{payload}));
        view.payload.reset();
        REQUIRE(verify_sign1<crypto_esp256_backend>(key.get(), view, aad, std::span<const std::byte>{payload}));
    }
#endif

} // TEST_SUITE("roundtrip/cwt")

#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
TEST_SUITE("roundtrip/cwt") {
    TEST_CASE("openssl consumes single pass signature input") {
        auto                                            key = make_p256_key();
        const std::vector<std::byte>                    bytes(1024, std::byte{7});
        const std::array<std::span<const std::byte>, 4> spans{std::span<const std::byte>{}, std::span<const std::byte>{bytes}.first(33),
                                                              std::span<const std::byte>{bytes}.subspan(33), std::span<const std::byte>{}};
        std::istringstream                              order{"0 1 2 3"};
        auto parts = std::ranges::istream_view<std::size_t>(order) | std::views::transform([&](std::size_t index) { return spans[index]; });
        static_assert(!std::ranges::forward_range<decltype(parts)>);

        auto signature = crypto_esp256_backend::sign(key.get(), parts);
        REQUIRE(signature);
        const std::array contiguous{std::span<const std::byte>{bytes}};
        REQUIRE(crypto_esp256_backend::verify(key.get(), contiguous, *signature));

        std::istringstream verification_order{"0 1 2 3"};
        auto               verification_parts = std::ranges::istream_view<std::size_t>(verification_order) |
                                                std::views::transform([&](std::size_t index) { return spans[index]; });
        REQUIRE(crypto_esp256_backend::verify(key.get(), verification_parts, *signature));
    }
}
#endif
