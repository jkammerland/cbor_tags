#include <cbor_tags/cwt/thumbprint.h>
#include <doctest/doctest.h>
#ifdef CBOR_TAGS_TEST_HAS_CWT_OPENSSL
#include <cbor_tags/cwt/openssl_sha256.h>
#endif

using namespace cbor::tags;

namespace {
std::vector<std::byte> unhex(std::string_view text) {
    const auto             digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    std::vector<std::byte> result;
    for (std::size_t i = 0; i < text.size(); i += 2)
        result.push_back(static_cast<std::byte>(digit(text[i]) * 16 + digit(text[i + 1])));
    return result;
}
std::vector<std::byte> flatten(const byte_segments &parts) {
    std::vector<std::byte> result;
    for (const auto &part : parts)
        result.insert(result.end(), part.bytes().begin(), part.bytes().end());
    return result;
}
cwt::ec2_public_key rfc_key() {
    cwt::ec2_public_key key;
    key.x = unhex("65eda5a12577c2bae829437fe338701a10aaa375e1bb5b5de108de439c08551d");
    key.y = unhex("1e52ed75701163f7f9e40ddf9f341b3dc9ba860af7e0ca7ca7e9eecd0084d19c");
    return key;
}
} // namespace

TEST_SUITE_BEGIN("cbor_wire/cwt/keys");

TEST_CASE("cwt thumbprint wire encodes RFC 9679 fields without metadata") {
    auto key    = rfc_key();
    key.alg     = cwt::algorithm::esp256;
    key.kid     = unhex("deadbeef");
    key.key_ops = std::vector<std::int64_t>{2};

    auto parts = cwt::make_key_thumbprint_input(key);
    REQUIRE(parts);
    CHECK(flatten(*parts) == unhex("a40102200121582065eda5a12577c2bae829437fe338701a10aaa375e1bb5b5de108de439c08551d2258201e52ed75701163f7f"
                                   "9e40ddf9f341b3dc9ba860af7e0ca7ca7e9eecd0084d19c"));
    bool borrowed_x = false;
    for (const auto &part : *parts)
        borrowed_x |= part.bytes().data() == key.x.data();
    CHECK(borrowed_x);

    key.y = false;
    CHECK_FALSE(cwt::make_key_thumbprint_input(key));
    key       = rfc_key();
    key.curve = "P-256";
    CHECK_FALSE(cwt::make_key_thumbprint_input(key));
}

TEST_CASE("cwt thumbprint wire encodes rsa and okp required parameters") {
    cwt::rsa_public_key rsa;
    rsa.modulus    = unhex("8001");
    rsa.exponent   = unhex("010001");
    auto rsa_parts = cwt::make_key_thumbprint_input(rsa);
    REQUIRE(rsa_parts);
    CHECK(flatten(*rsa_parts) == unhex("a30103204280012143010001"));

    cwt::okp_public_key okp;
    okp.x          = unhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
    auto okp_parts = cwt::make_key_thumbprint_input(okp);
    REQUIRE(okp_parts);
    CHECK(flatten(*okp_parts) == unhex("a301012006215820d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"));
}

#ifdef CBOR_TAGS_TEST_HAS_CWT_OPENSSL
TEST_CASE("cwt thumbprint sha256 agrees with RFC 9679 and independent rsa okp fixtures") {
    auto key    = rfc_key();
    auto digest = cwt::key_thumbprint<cwt::openssl_sha256>(key);
    REQUIRE(digest);
    CHECK(std::ranges::equal(*digest, unhex("496bd8afadf307e5b08c64b0421bf9dc01528a344a43bda88fadd1669da253ec")));

    cwt::rsa_public_key rsa;
    rsa.modulus     = unhex("8001");
    rsa.exponent    = unhex("010001");
    auto rsa_digest = cwt::key_thumbprint<cwt::openssl_sha256>(rsa);
    REQUIRE(rsa_digest);
    CHECK(std::ranges::equal(*rsa_digest, unhex("40a7070231b6fd4f48b3d5302b0be8b6cb8980ff345ef43416aaaf578e1cdb14")));

    cwt::okp_public_key okp;
    okp.x           = unhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
    auto okp_digest = cwt::key_thumbprint<cwt::openssl_sha256>(okp);
    REQUIRE(okp_digest);
    CHECK(std::ranges::equal(*okp_digest, unhex("866eefbd6718c8846cd7ddfe43fc74ab1daac4538ff8514ea2ec2d410a415743")));
}

#endif

TEST_SUITE_END();
