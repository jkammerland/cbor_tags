#include <array>
#include <string>
#include <cbor_tags/cbor_decoder.h>
#include <doctest/doctest.h>
#include <vector>

using namespace cbor::tags;

TEST_CASE("decoder should accept empty byte strings") {
    std::vector<std::byte> buffer{std::byte{0x40}}; // 0-length bstr

    auto dec = make_decoder(buffer);

    std::vector<std::byte> decoded;
    auto                   result = dec(decoded);

    CHECK_MESSAGE(result, "Decoding an empty byte string should succeed.");
    CHECK(decoded.empty());
}

TEST_CASE("decoder should accept empty text strings") {
    std::vector<std::byte> buffer{std::byte{0x60}}; // 0-length tstr

    auto dec = make_decoder(buffer);

    std::string decoded;
    auto              result = dec(decoded);

    CHECK_MESSAGE(result, "Decoding an empty text string should succeed.");
    CHECK(decoded.empty());
}

TEST_CASE("decoder should reject undersized byte strings for fixed arrays") {
    std::vector<std::byte> buffer{std::byte{0x41}, std::byte{0x01}}; // length 1, value 0x01

    auto dec = make_decoder(buffer);

    std::array<std::byte, 2> decoded{};
    decoded.fill(std::byte{0xAA});

    auto result = dec(decoded);

    CHECK_FALSE_MESSAGE(result, "Decoding into a larger fixed array should flag the size mismatch.");
    CHECK_EQ(result.error(), status_code::unexpected_group_size);
    CHECK_EQ(decoded[0], std::byte{0xAA});
    CHECK_EQ(decoded[1], std::byte{0xAA});
}
