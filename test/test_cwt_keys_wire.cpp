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

TEST_CASE("cwt public key wire rejects missing type private parameters and duplicate unknown labels") {
    for (auto wire :
         {bytes({0xa2, 0x20, 0x41, 0x80, 0x21, 0x41, 3}), bytes({0xa4, 1, 3, 0x20, 0x41, 0x80, 0x21, 0x41, 3, 0x22, 0x41, 1}),
          bytes({0xa5, 1, 3, 0x20, 0x41, 0x80, 0x21, 0x41, 3, 9, 0, 9, 0}), bytes({0xa3, 1, 3, 0x20, 0x42, 0, 0x80, 0x21, 0x41, 3})}) {
        cwt::rsa_public_key key;
        CHECK_FALSE(make_decoder<codec::cwt>(wire)(key));
    }
}

TEST_CASE("cwt public key wire rejects truncation and accepts indefinite maps") {
    auto                wire = bytes({0xbf, 1, 3, 0x20, 0x41, 0x80, 0x21, 0x41, 3, 0xff});
    cwt::rsa_public_key value;
    REQUIRE(make_decoder<codec::cwt>(wire)(value));
    for (std::size_t size = 0; size < wire.size(); ++size) {
        auto prefix = std::span<const std::byte>{wire}.first(size);
        CHECK_FALSE(make_decoder<codec::cwt>(prefix)(value));
    }
}

TEST_SUITE_END();
