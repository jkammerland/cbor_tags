#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

using namespace cbor::tags;

TEST_CASE("stream decode rolls back on incomplete") {
    std::vector<std::byte> encoded;
    auto                   enc = make_encoder(encoded);
    REQUIRE(enc(wrap_as_array(1u, 2u)));

    std::vector<std::byte> buffer;
    buffer.reserve(encoded.size());
    auto dec = make_decoder(buffer);

    std::uint64_t a{};
    std::uint64_t b{};

    auto op = dec.stream_decode(wrap_as_array(a, b));

    CHECK_EQ(op.resume(), status_code::incomplete);
    CHECK_EQ(a, 0);
    CHECK_EQ(b, 0);

    for (std::size_t i = 0; i < encoded.size(); ++i) {
        buffer.push_back(encoded[i]);
        auto status = op.resume();
        if (i + 1 < encoded.size()) {
            CHECK_EQ(status, status_code::incomplete);
            CHECK_EQ(a, 0);
            CHECK_EQ(b, 0);
        } else {
            CHECK_EQ(status, status_code::success);
            CHECK_EQ(a, 1);
            CHECK_EQ(b, 2);
        }
    }
}

TEST_CASE("stream decode keeps prior args on later incomplete") {
    std::vector<std::byte> encoded;
    auto                   enc = make_encoder(encoded);
    REQUIRE(enc(1u, 2u));

    std::vector<std::byte> buffer;
    buffer.reserve(encoded.size());
    auto dec = make_decoder(buffer);

    std::uint64_t a{};
    std::uint64_t b{};
    auto          op = dec.stream_decode(a, b);

    buffer.push_back(encoded[0]);
    CHECK_EQ(op.resume(), status_code::incomplete);
    CHECK_EQ(a, 1);
    CHECK_EQ(b, 0);

    buffer.push_back(encoded[1]);
    CHECK_EQ(op.resume(), status_code::success);
    CHECK_EQ(a, 1);
    CHECK_EQ(b, 2);
}

