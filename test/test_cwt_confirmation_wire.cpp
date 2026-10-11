#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/codec/cwt.h>
#include <doctest/doctest.h>

using namespace cbor::tags;

namespace {
std::vector<std::byte> bytes(std::initializer_list<unsigned char> value) {
    std::vector<std::byte> result;
    for (auto byte : value)
        result.push_back(static_cast<std::byte>(byte));
    return result;
}
} // namespace

TEST_SUITE_BEGIN("cbor_wire/cwt/keys");

TEST_CASE("cwt confirmation wire rejects inline and encrypted keys together") {
    auto                                   wire = bytes({0xa2, 1, 0xa3, 1, 3, 0x20, 0x41, 0x80, 0x21, 0x41, 3, 2, 0xf6});
    cwt::confirmation<cwt::rsa_public_key> value;
    CHECK_FALSE(make_decoder<codec::cwt>(wire)(value));
}

TEST_SUITE_END();
