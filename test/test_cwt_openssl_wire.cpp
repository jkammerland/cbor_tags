#include "cwt_openssl_test_support.h"
#include "test_util.h"

using namespace cwt_test;

TEST_SUITE("cbor_wire/cwt") {

#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
    TEST_CASE("indefinite COSE framing preserves a real ES256 signature") {
        auto              key = make_p256_key();
        const byte_string payload{std::byte{'a'}, std::byte{'b'}};
        auto              source = sign1<crypto_es256_backend>(key.get(), {}, {}, payload);
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
        CHECK(*before == *after);
        REQUIRE(verify_sign1<crypto_es256_backend>(key.get(), decoded));
    }
#endif

} // TEST_SUITE("cbor_wire/cwt")
