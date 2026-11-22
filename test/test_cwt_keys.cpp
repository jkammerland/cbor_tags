#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/codec/cwt.h>
#include <deque>
#include <doctest/doctest.h>

using namespace cbor::tags;

TEST_SUITE_BEGIN("roundtrip/cwt/keys");

TEST_CASE("cwt public key owning and borrowed round trips") {
    cwt::ec2_public_key source;
    source.x.assign(32, std::byte{3});
    source.y       = std::vector<std::byte>(32, std::byte{4});
    source.kid     = std::vector{std::byte{5}};
    source.alg     = cwt::algorithm::esp256;
    source.key_ops = std::vector<std::int64_t>{2};

    std::vector<std::byte> buffer;
    REQUIRE(make_encoder<codec::cwt>(buffer)(source));

    cwt::ec2_public_key_view received;
    REQUIRE(make_decoder<codec::cwt>(buffer)(received));
    CHECK(std::ranges::equal(received.x, source.x));
    CHECK(received.x.data() >= buffer.data());
    CHECK(received.x.data() + received.x.size() <= buffer.data() + buffer.size());
    CHECK(received.alg == source.alg);
    CHECK(received.key_ops == source.key_ops);
    REQUIRE(cwt::validate_key_use(received, cwt::algorithm::esp256));
    CHECK_FALSE(cwt::validate_key_use(received, cwt::algorithm::es256));

    std::deque<std::byte> chunks(buffer.begin(), buffer.end());
    cwt::ec2_public_key   owned;
    REQUIRE(make_decoder<codec::cwt>(chunks)(owned));
    CHECK(owned.x == source.x);
    CHECK(owned.y == source.y);
    CHECK_FALSE(make_decoder<codec::cwt>(chunks)(received));
}

TEST_CASE("cwt public keys preserve compressed points and key restrictions") {
    cwt::ec2_public_key source;
    source.x.assign(32, std::byte{3});
    source.y = true;
    source.key_ops.emplace();

    std::vector<std::byte> buffer;
    REQUIRE(make_encoder<codec::cwt>(buffer)(source));
    cwt::ec2_public_key received;
    REQUIRE(make_decoder<codec::cwt>(buffer)(received));
    CHECK(std::get<bool>(received.y));
    CHECK_FALSE(cwt::validate_key_use(received, cwt::algorithm::esp256));
}

TEST_CASE("key use requires the key type and curve of the algorithm") {
    cwt::ec2_public_key p256;
    p256.x.assign(32, std::byte{3});
    p256.y = std::vector<std::byte>(32, std::byte{4});

    cwt::ec2_public_key p384;
    p384.curve = 2;
    p384.x.assign(48, std::byte{3});
    p384.y = std::vector<std::byte>(48, std::byte{4});

    cwt::okp_public_key ed25519;
    ed25519.x.assign(32, std::byte{7});

    cwt::rsa_public_key rsa;
    rsa.modulus  = {std::byte{0x80}, std::byte{1}};
    rsa.exponent = {std::byte{3}};

    // ESP256 is ECDSA with P-256, and each fully specified algorithm is as exact
    CHECK(cwt::validate_key_use(p256, cwt::algorithm::esp256));
    CHECK_FALSE(cwt::validate_key_use(p384, cwt::algorithm::esp256));
    CHECK_FALSE(cwt::validate_key_use(ed25519, cwt::algorithm::esp256));
    CHECK_FALSE(cwt::validate_key_use(rsa, cwt::algorithm::esp256));
    CHECK(cwt::validate_key_use(p384, cwt::algorithm::esp384));
    CHECK_FALSE(cwt::validate_key_use(p256, cwt::algorithm::esp384));
    CHECK(cwt::validate_key_use(ed25519, cwt::algorithm::ed25519));
    CHECK_FALSE(cwt::validate_key_use(ed25519, cwt::algorithm::ed448));
    CHECK_FALSE(cwt::validate_key_use(p256, cwt::algorithm::ed25519));

    // The older identifiers leave the curve open, not the kind of key
    CHECK(cwt::validate_key_use(p256, cwt::algorithm::es256));
    CHECK(cwt::validate_key_use(p384, cwt::algorithm::es256));
    CHECK_FALSE(cwt::validate_key_use(ed25519, cwt::algorithm::es256));
    CHECK(cwt::validate_key_use(ed25519, cwt::algorithm::eddsa));
    CHECK_FALSE(cwt::validate_key_use(p256, cwt::algorithm::eddsa));

    // A curve with a name of its own is not P-256
    cwt::ec2_public_key named = p256;
    named.curve               = "application-curve";
    CHECK_FALSE(cwt::validate_key_use(named, cwt::algorithm::esp256));
    CHECK(cwt::validate_key_use(named, cwt::algorithm::es256));
}

TEST_CASE("cwt rsa and okp public keys round trip") {
    cwt::rsa_public_key rsa;
    rsa.modulus  = {std::byte{0x80}, std::byte{1}};
    rsa.exponent = {std::byte{3}};
    cwt::okp_public_key okp;
    okp.x.assign(32, std::byte{7});

    std::vector<std::byte> buffer;
    REQUIRE(make_encoder<codec::cwt>(buffer)(rsa, okp));
    cwt::rsa_public_key decoded_rsa;
    cwt::okp_public_key decoded_okp;
    REQUIRE(make_decoder<codec::cwt>(buffer)(decoded_rsa, decoded_okp));
    CHECK(decoded_rsa.modulus == rsa.modulus);
    CHECK(decoded_rsa.exponent == rsa.exponent);
    CHECK(decoded_okp.x == okp.x);
    CHECK(decoded_okp.curve == okp.curve);
}

TEST_CASE("cwt public key views borrow named owners") {
    cwt::ec2_public_key key;
    key.x.assign(32, std::byte{7});
    key.y     = std::vector<std::byte>(32, std::byte{8});
    key.kid   = std::vector{std::byte{9}};
    auto view = cwt::as_view(key);
    CHECK(view.x.data() == key.x.data());
    CHECK(std::get<std::span<const std::byte>>(view.y).data() == std::get<std::vector<std::byte>>(key.y).data());
    REQUIRE(view.kid);
    CHECK(view.kid->data() == key.kid->data());

    key.curve = "application-curve";
    view      = cwt::as_view(key);
    CHECK(std::get<std::string_view>(view.curve).data() == std::get<std::string>(key.curve).data());
}

TEST_CASE("cwt public keys reject known curves from a different key family") {
    cwt::ec2_public_key ec;
    ec.x.assign(32, std::byte{7});
    ec.y     = true;
    ec.curve = 6;
    cwt::okp_public_key okp;
    okp.x.assign(32, std::byte{7});
    okp.curve = 1;

    std::vector<std::byte> buffer;
    auto                   encoder = make_encoder<codec::cwt>(buffer);
    CHECK_FALSE(encoder(ec));
    CHECK_FALSE(encoder(okp));
}

TEST_SUITE_END();
