#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cstddef>
#include <doctest/doctest.h>
#include <vector>

using namespace cbor::tags;

TEST_CASE_TEMPLATE("ordinary floating arrays roundtrip with reserved output", Float, float, double) {
    for (const std::size_t count : {0U, 1U, 32U, 4096U}) {
        CAPTURE(count);
        std::vector<Float> source;
        source.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            source.push_back(static_cast<Float>(static_cast<int>(index % 257) - 128) / Float{8});
        }
        std::vector<std::byte> output;
        output.reserve(count * (sizeof(Float) + 1) + 9);
        REQUIRE(make_encoder(output)(source));
        std::vector<Float> decoded;
        REQUIRE(make_decoder(output)(decoded));
        CHECK(decoded == source);
    }
}

TEST_CASE_TEMPLATE("ordinary floating arrays roundtrip with exact fixed output", Float, float, double) {
    const std::array<Float, 3>                                     source{Float{1}, Float{-2.5}, Float{0}};
    std::array<std::byte, 1 + source.size() * (sizeof(Float) + 1)> output{};
    REQUIRE(make_encoder(output)(source));
    std::array<Float, 3> decoded{};
    REQUIRE(make_decoder(output)(decoded));
    CHECK(decoded == source);
}
