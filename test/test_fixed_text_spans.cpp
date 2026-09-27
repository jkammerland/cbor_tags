#include <algorithm>
#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cstddef>
#include <doctest/doctest.h>
#include <span>
#include <vector>

using namespace cbor::tags;

TEST_SUITE("roundtrip/fixed_text_spans") {

    TEST_CASE("fixed-extent text spans roundtrip character values") {
        const std::array<char, 3>      value{'a', '\0', 'z'};
        const std::span<const char, 3> input{value};
        std::vector<std::byte>         bytes;
        REQUIRE(make_encoder(bytes)(input));

        const std::array<char, 3> initial{'x', 'y', 'z'};
        std::span<const char, 3>  output{initial};
        SUBCASE("ordinary decoding") { REQUIRE(make_decoder(bytes)(output)); }
        SUBCASE("bounded decoding") { REQUIRE(make_decoder(bytes)(as_bounded_size(output, 0, 4))); }
        CHECK(std::ranges::equal(output, input));
    }

    TEST_CASE("zero-extent text spans roundtrip empty values") {
        const std::span<const char, 0> input;
        std::vector<std::byte>         bytes;
        REQUIRE(make_encoder(bytes)(input));

        std::span<const char, 0> output;
        SUBCASE("ordinary decoding") { REQUIRE(make_decoder(bytes)(output)); }
        SUBCASE("bounded decoding") { REQUIRE(make_decoder(bytes)(as_bounded_size(output, 0, 0))); }
        CHECK_EQ(output.size(), input.size());
        CHECK(output.empty());
    }

} // TEST_SUITE
