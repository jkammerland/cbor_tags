#include "cwt_openssl_test_support.h"
#include "test_util.h"

#include <array>

using namespace cwt_test;

TEST_SUITE("cbor_wire/cwt") {

#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
    TEST_CASE("indefinite COSE framing preserves a real ES256 signature") {
        auto                         key = make_p256_key();
        const std::vector<std::byte> payload{std::byte{'a'}, std::byte{'b'}};
        auto                         source = sign1<crypto_es256_backend>(key.get(), {}, {}, payload);
        REQUIRE(source);
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
        REQUIRE(verify_sign1<crypto_es256_backend>(key.get(), decoded));
    }
    TEST_CASE("verification preserves nonminimal protected header label encoding") {
        auto                         key = make_p256_key();
        const std::vector<std::byte> signed_bytes{std::byte{0x84}, std::byte{0x6a}, std::byte{'S'},  std::byte{'i'},  std::byte{'g'},
                                                  std::byte{'n'},  std::byte{'a'},  std::byte{'t'},  std::byte{'u'},  std::byte{'r'},
                                                  std::byte{'e'},  std::byte{'1'},  std::byte{0x44}, std::byte{0xa1}, std::byte{0x18},
                                                  std::byte{0x01}, std::byte{0x26}, std::byte{0x40}, std::byte{0x41}, std::byte{0x07}};
        const std::array             parts{std::span<const std::byte>{signed_bytes}};
        auto                         signature = crypto_es256_backend::sign(key.get(), parts);
        REQUIRE(signature);

        cose_sign1 message{.protected_header = {std::byte{0xa1}, std::byte{0x18}, std::byte{0x01}, std::byte{0x26}},
                           .unprotected      = {},
                           .payload          = std::vector<std::byte>{std::byte{0x07}},
                           .signature        = std::move(*signature)};
        REQUIRE(verify_sign1<crypto_es256_backend>(key.get(), message));
        message.protected_header = {std::byte{0xa1}, std::byte{0x01}, std::byte{0x26}};
        CHECK_FALSE(verify_sign1<crypto_es256_backend>(key.get(), message));
    }
#endif

} // TEST_SUITE("cbor_wire/cwt")
