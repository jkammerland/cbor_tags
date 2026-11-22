#include "cwt_openssl_test_support.h"
#include "test_util.h"

#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
#include <cbor_tags/cwt/openssl_keys.h>
#endif

#include <array>

using namespace cwt_test;

TEST_SUITE("cbor_wire/cwt") {

#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
    // Messages and keys as the RFCs print them. Signatures nobody here made
    TEST_CASE("published RFC 8392 and RFC 9052 ES256 vectors verify") {
        const auto published_key = [](std::string_view x, std::string_view y) {
            ec2_public_key key;
            key.x = to_bytes(x);
            key.y = to_bytes(y);
            return key;
        };

        SUBCASE("RFC 8392 A.3, a signed CWT, with the key of A.2.3") {
            const auto wire =
                to_bytes("d28443a10126a104524173796d6d657472696345434453413235365850a70175636f61703a2f2f61732e6578616d706c652e636f6d"
                         "02656572696b77037818636f61703a2f2f6c696768742e6578616d706c652e636f6d041a5612aeb0051a5610d9f0061a5610d9f0"
                         "07420b7158405427c1ff28d23fbad1f29c4c7c6a555e601d6fa29f9179bc3d7438bacaca5acd08c8d4d4f96131680c429a01f859"
                         "51ecee743a52b9b63632c57209120e1c9e30");
            const auto key = import_p256_public_key(published_key("143329cce7868e416927599cf65a34f3ce2ffda55a7eca69ed8919a394d42f0f",
                                                                  "60f7f1a780d8a783bfb7a2dd6b2796e8128dbbcef9d3d168db9529971a36e7b9"),
                                                    algorithm::es256);
            REQUIRE(key);

            cose_sign1 message;
            auto       tagged  = make_tag_pair(cose_sign1_tag{}, message);
            auto       decoder = make_decoder<codec::cwt>(wire);
            auto       decoded = decoder(tagged);
            REQUIRE(decoded);
            CHECK(decoder.tell() == wire.end());
            CHECK(verify_sign1<openssl_es256_verifier>(*key, message));

            REQUIRE(message.payload);
            claims_set claims;
            auto       claims_decoder = make_decoder<codec::cwt>(*message.payload);
            auto       claims_result  = claims_decoder(claims);
            REQUIRE(claims_result);
            CHECK(claims.issuer == "coap://as.example.com");
            CHECK(claims.subject == "erikw");
            CHECK(claims.expiration == numeric_date{std::int64_t{1444064944}});

            // One bit of the payload and the signature is no longer for it
            message.payload->at(10) ^= std::byte{1};
            CHECK_FALSE(verify_sign1<openssl_es256_verifier>(*key, message));
        }

        SUBCASE("RFC 9052 C.2.1, a COSE_Sign1, with key 11") {
            const auto wire =
                to_bytes("d28443a10126a10442313154546869732069732074686520636f6e74656e742e58408eb33e4ca31d1c465ab05aac34cc6b23d58f"
                         "ef5c083106c4d25a91aef0b0117e2af9a291aa32e14ab834dc56ed2a223444547e01f11d3b0916e5a4c345cacb36");
            const auto key = import_p256_public_key(published_key("bac5b11cad8f99f9c72b05cf4b9e26d244dc189f745228255a219a86d6a09eff",
                                                                  "20138bf82dc1b6d562be0fa54ab7804a3a64b6d72ccfed6b6fb6ed28bbfc117e"),
                                                    algorithm::es256);
            REQUIRE(key);

            cose_sign1 message;
            auto       tagged  = make_tag_pair(cose_sign1_tag{}, message);
            auto       decoder = make_decoder<codec::cwt>(wire);
            auto       decoded = decoder(tagged);
            REQUIRE(decoded);
            CHECK(decoder.tell() == wire.end());
            CHECK(verify_sign1<openssl_es256_verifier>(*key, message));

            // "This is the content." with its first letter changed
            REQUIRE(message.payload);
            message.payload->front() ^= std::byte{1};
            CHECK_FALSE(verify_sign1<openssl_es256_verifier>(*key, message));
        }

        SUBCASE("RFC 9052 C.1.1, a COSE_Sign with one signer, with key 11") {
            const auto wire =
                to_bytes("d8628440a054546869732069732074686520636f6e74656e742e818343a10126a1044231315840e2aeafd40d69d19dfe6e52077c5d"
                         "7ff4e408282cbefb5d06cbf414af2e19d982ac45ac98b8544c908b4507de1e90b717c3d34816fe926a2b98f53afd2fa0f30a");
            const auto key = import_p256_public_key(published_key("bac5b11cad8f99f9c72b05cf4b9e26d244dc189f745228255a219a86d6a09eff",
                                                                  "20138bf82dc1b6d562be0fa54ab7804a3a64b6d72ccfed6b6fb6ed28bbfc117e"),
                                                    algorithm::es256);
            REQUIRE(key);

            cose_sign message;
            auto      tagged  = make_tag_pair(cose_sign_tag{}, message);
            auto      decoder = make_decoder<codec::cwt>(wire);
            auto      decoded = decoder(tagged);
            REQUIRE(decoded);
            CHECK(decoder.tell() == wire.end());
            REQUIRE(message.signatures.size() == 1);
            CHECK(verify_sign<openssl_es256_verifier>(*key, message));

            REQUIRE(message.payload);
            message.payload->front() ^= std::byte{1};
            CHECK_FALSE(verify_sign<openssl_es256_verifier>(*key, message));
        }
    }

    TEST_CASE("indefinite COSE framing preserves a real ESP256 signature") {
        auto                         key = make_p256_key();
        const std::vector<std::byte> payload{std::byte{'a'}, std::byte{'b'}};
        auto                         source = sign1<crypto_esp256_backend>(key.get(), {}, {}, payload);
        REQUIRE(source);
        const std::vector<std::byte> expected_header{std::byte{0xa1}, std::byte{0x01}, std::byte{0x28}};
        CHECK(source->protected_header == expected_header);
        const auto before = make_sign1_tbs(*source);
        REQUIRE(before);
        auto encoded = encode_to_bytes(*source);
        REQUIRE(encoded);
        REQUIRE(encoded->front() == std::byte{0x84});
        encoded->front() = std::byte{0x9f};
        encoded->push_back(std::byte{0xff});
        cose_sign1 decoded;
        REQUIRE(make_decoder<codec::cwt>(*encoded)(decoded));
        CHECK(decoded.payload == source->payload);
        CHECK(decoded.signature == source->signature);
        const auto after = make_sign1_tbs(decoded);
        REQUIRE(after);
        CHECK(before->flatten() == after->flatten());
        REQUIRE(verify_sign1<crypto_esp256_backend>(key.get(), decoded));
    }
    TEST_CASE("verification preserves nonminimal protected header label encoding") {
        auto                         key = make_p256_key();
        const std::vector<std::byte> signed_bytes{std::byte{0x84}, std::byte{0x6a}, std::byte{'S'},  std::byte{'i'},  std::byte{'g'},
                                                  std::byte{'n'},  std::byte{'a'},  std::byte{'t'},  std::byte{'u'},  std::byte{'r'},
                                                  std::byte{'e'},  std::byte{'1'},  std::byte{0x44}, std::byte{0xa1}, std::byte{0x18},
                                                  std::byte{0x01}, std::byte{0x26}, std::byte{0x40}, std::byte{0x41}, std::byte{0x07}};
        const std::array             parts{std::span<const std::byte>{signed_bytes}};
        auto                         signature = crypto_esp256_backend::sign(key.get(), parts);
        REQUIRE(signature);

        cose_sign1 message{.protected_header = {std::byte{0xa1}, std::byte{0x18}, std::byte{0x01}, std::byte{0x26}},
                           .unprotected      = {},
                           .payload          = std::vector<std::byte>{std::byte{0x07}},
                           .signature        = std::move(*signature)};
        REQUIRE(verify_sign1<openssl_es256_verifier>(key.get(), message));
        CHECK_FALSE(verify_sign1<crypto_esp256_backend>(key.get(), message));
        message.protected_header = {std::byte{0xa1}, std::byte{0x01}, std::byte{0x26}};
        CHECK_FALSE(verify_sign1<openssl_es256_verifier>(key.get(), message));
    }
#endif

} // TEST_SUITE("cbor_wire/cwt")
