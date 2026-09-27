#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cstddef>
#include <deque>
#include <doctest/doctest.h>
#include <list>
#include <map>
#include <optional>
#include <vector>

using namespace cbor::tags;

namespace {
struct point {
    int  x{};
    int  y{};
    bool operator==(const point &) const = default;
};
struct node {
    int               id{};
    std::vector<node> children;
    bool              operator==(const node &) const = default;
};
} // namespace

TEST_SUITE("roundtrip/aggregate_decode") {

    TEST_CASE_TEMPLATE("aggregate containers preserve typed values and following items", Buffer, std::vector<std::byte>,
                       std::deque<std::byte>, std::list<std::byte>) {
        const std::vector<point>   array{{1, 2}, {3, 4}};
        const std::map<int, point> map{{3, {5, 6}}};
        const std::optional<point> optional{point{7, 8}};
        std::vector<std::byte>     bytes;
        REQUIRE(make_encoder(bytes)(array, map, optional, 9));
        const Buffer         input(bytes.begin(), bytes.end());
        auto                 dec = make_decoder(input);
        std::vector<point>   decoded_array;
        std::map<int, point> decoded_map;
        std::optional<point> decoded_optional;
        int                  following{};
        REQUIRE(dec(decoded_array, decoded_map, decoded_optional, following));
        CHECK_EQ(decoded_array, array);
        CHECK_EQ(decoded_map, map);
        CHECK_EQ(decoded_optional, optional);
        CHECK_EQ(following, 9);
    }

    TEST_CASE("recursive aggregates preserve nested children") {
        const node             original{1, {{2, {{3, {}}}}, {4, {}}}};
        std::vector<std::byte> bytes;
        REQUIRE(make_encoder(bytes)(original));
        node decoded;
        REQUIRE(make_decoder(bytes)(decoded));
        CHECK_EQ(decoded, original);
    }

} // TEST_SUITE
