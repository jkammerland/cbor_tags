#ifdef CBOR_TAGS_TEST_HAS_CWT_OPENSSL
#include <cbor_tags/cwt/openssl_keys.h>
#include <doctest/doctest.h>
using namespace cbor::tags;
namespace {
std::vector<std::byte> unhex(std::string_view text) {
    const auto             digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    std::vector<std::byte> result;
    for (std::size_t i = 0; i < text.size(); i += 2)
        result.push_back(static_cast<std::byte>(digit(text[i]) * 16 + digit(text[i + 1])));
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

TEST_CASE("cwt checked p256 import enforces metadata and expands compressed points") {
    auto key      = rfc_key();
    key.alg       = cwt::algorithm::esp256;
    key.key_ops   = std::vector<std::int64_t>{2};
    auto imported = cwt::import_p256_public_key(key);
    REQUIRE(imported);

    key.y         = false;
    auto expanded = cwt::expand_p256_public_key(key);
    REQUIRE(expanded);
    auto reference = rfc_key();
    CHECK(expanded->y == reference.y);
    auto digest = cwt::key_thumbprint<cwt::openssl_sha256>(*expanded);
    REQUIRE(digest);
    CHECK(std::ranges::equal(*digest, unhex("496bd8afadf307e5b08c64b0421bf9dc01528a344a43bda88fadd1669da253ec")));

    key.key_ops = std::vector<std::int64_t>{1};
    CHECK_FALSE(cwt::import_p256_public_key(key));
    key.key_ops.reset();
    key.alg = cwt::algorithm::es256;
    CHECK_FALSE(cwt::import_p256_public_key(key));
    CHECK(cwt::import_p256_public_key(key, cwt::algorithm::es256));
    key.x.assign(32, std::byte{0xff});
    CHECK_FALSE(cwt::import_p256_public_key(key, cwt::algorithm::es256));
}
TEST_SUITE_END();
#endif
