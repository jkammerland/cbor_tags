#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/codec/cwt.h>
#include <doctest/doctest.h>

using namespace cbor::tags;

TEST_SUITE_BEGIN("roundtrip/cwt/keys");

TEST_CASE("cwt confirmation keeps independently supplied methods") {
    cwt::confirmation<cwt::rsa_public_key> source;
    source.key.emplace();
    source.key->modulus  = {std::byte{0x80}, std::byte{1}};
    source.key->exponent = {std::byte{3}};
    source.kid           = std::vector{std::byte{7}};
    source.thumbprint    = std::vector<std::byte>(32, std::byte{9});

    std::vector<std::byte> buffer;
    REQUIRE(make_encoder<codec::cwt>(buffer)(source));
    cwt::confirmation_view<cwt::rsa_public_key_view> received;
    REQUIRE(make_decoder<codec::cwt>(buffer)(received));
    REQUIRE(received.key);
    REQUIRE(received.kid);
    REQUIRE(received.thumbprint);
    CHECK(std::ranges::equal(received.key->modulus, source.key->modulus));
    CHECK(std::ranges::equal(*received.kid, *source.kid));
    CHECK(std::ranges::equal(*received.thumbprint, *source.thumbprint));
}

TEST_SUITE_END();
