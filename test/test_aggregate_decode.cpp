#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cstddef>
#include <deque>
#include <doctest/doctest.h>
#include <list>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

using namespace cbor::tags;

namespace {

struct point {
    int x{};
    int y{};

    bool operator==(const point &) const = default;
};

struct nested_point {
    point value;
};

} // namespace

TEST_CASE_TEMPLATE("untagged aggregates decode in definite and indefinite arrays", Buffer, std::vector<std::byte>, std::deque<std::byte>,
                   std::list<std::byte>) {
    for (const auto *hex : {"8182010207", "9f820102ff07"}) {
        CAPTURE(hex);
        const auto         bytes = to_bytes(hex);
        const Buffer       input(bytes.begin(), bytes.end());
        std::vector<point> output;
        auto               dec = make_decoder(input);
        REQUIRE(dec(output));
        REQUIRE_EQ(output.size(), 1U);
        CHECK_EQ(output.front(), (point{1, 2}));
        int following{};
        REQUIRE(dec(following));
        CHECK_EQ(following, 7);

        std::vector<std::byte> encoded;
        REQUIRE(make_encoder(encoded)(output));
        CHECK_EQ(to_hex(encoded), "81820102");
    }
}

TEST_CASE("untagged aggregates decode in definite and indefinite maps") {
    for (const auto *hex : {"a10382010207", "bf03820102ff07"}) {
        CAPTURE(hex);
        const auto           input = to_bytes(hex);
        std::map<int, point> output;
        auto                 dec = make_decoder(input);
        REQUIRE(dec(output));
        REQUIRE_EQ(output.size(), 1U);
        CHECK_EQ(output.at(3), (point{1, 2}));
        int following{};
        REQUIRE(dec(following));
        CHECK_EQ(following, 7);
    }
}

TEST_CASE("single-field aggregates and tuples forward consumed headers") {
    const auto input = to_bytes("9f820102ff07");
    SUBCASE("single-field aggregate wrapping another aggregate") {
        std::vector<nested_point> output;
        auto                      dec = make_decoder(input);
        REQUIRE(dec(output));
        REQUIRE_EQ(output.size(), 1U);
        CHECK_EQ(output.front().value, (point{1, 2}));
        int following{};
        REQUIRE(dec(following));
        CHECK_EQ(following, 7);
    }
    SUBCASE("tuple in an indefinite array") {
        std::vector<std::tuple<int, int>> output;
        auto                              dec = make_decoder(input);
        REQUIRE(dec(output));
        REQUIRE_EQ(output.size(), 1U);
        CHECK_EQ(output.front(), std::make_tuple(1, 2));
        int following{};
        REQUIRE(dec(following));
        CHECK_EQ(following, 7);
    }
}

TEST_CASE("optional untagged aggregates decode wrapped and unwrapped groups") {
    SUBCASE("default wrapped group") {
        const auto           input = to_bytes("82010207");
        std::optional<point> output;
        auto                 dec = make_decoder(input);
        REQUIRE(dec(output));
        REQUIRE(output.has_value());
        CHECK_EQ(*output, (point{1, 2}));
        int following{};
        REQUIRE(dec(following));
        CHECK_EQ(following, 7);
    }
    SUBCASE("explicit unwrapped group sequence") {
        const auto           input = to_bytes("010207");
        std::optional<point> output;
        auto                 dec = make_decoder_with_options<Options<default_expected>>(input);
        REQUIRE(dec(output));
        REQUIRE(output.has_value());
        CHECK_EQ(*output, (point{1, 2}));
        int following{};
        REQUIRE(dec(following));
        CHECK_EQ(following, 7);
    }
}

TEST_CASE("untagged aggregate containers reject malformed groups") {
    for (const auto &[hex, expected_status] : {
             std::pair{"9fa0ff", status_code::no_match_for_array_on_buffer},
             std::pair{"9f8101ff", status_code::unexpected_group_size},
             std::pair{"9f83010203ff", status_code::unexpected_group_size},
             std::pair{"9f8201", status_code::incomplete},
             std::pair{"9f82016161ff", status_code::no_match_for_int_on_buffer},
         }) {
        CAPTURE(hex);
        const auto         input = to_bytes(hex);
        std::vector<point> output;
        const auto         result = make_decoder(input)(output);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), expected_status);
        CHECK(output.empty());
    }
}
