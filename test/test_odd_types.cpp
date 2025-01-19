#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_integer.h"
#include "test_util.h"

#include <doctest/doctest.h>
#include <fmt/base.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <map>
#include <unordered_map>
#include <vector>

using namespace cbor::tags;

TEST_CASE_TEMPLATE("Multimaps", T, std::multimap<int, std::string>, std::unordered_multimap<int, std::string>) {
    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);

    T map = {{1, "one"}, {2, "two"}, {3, "three"}};
    REQUIRE(enc(map));

    auto dec = make_decoder(data);
    T    map_result;
    REQUIRE(dec(map_result));
    CHECK_EQ(map_result, map);
}

TEST_CASE_TEMPLATE("Nested maps", T, std::multimap<int, std::map<int, std::string>>,
                   std::unordered_multimap<int, std::multimap<int, std::string>>) {
    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);
    T                      map = {{1, {{1, "one"}, {2, "two"}, {3, "three"}}}, {2, {{4, "four"}, {5, "five"}, {6, "six"}}}};

    REQUIRE(enc(map));

    auto dec = make_decoder(data);
    T    map_result;

    REQUIRE(dec(map_result));
    CHECK_EQ(map_result, map);
}

TEST_CASE("Map with variant") {
    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);

    std::map<int, std::variant<int, std::string>> map = {{1, 1}, {2, "two"}, {3, "THE THREE"}};
    REQUIRE(enc(map));
    REQUIRE_EQ(to_hex(data), "a30101026374776f0369544845205448524545");

    fmt::print("Map with variant: {}\n", to_hex(data));

    auto                                          dec = make_decoder(data);
    std::map<int, std::variant<int, std::string>> map_result;
    REQUIRE(dec(map_result));
    CHECK_EQ(map_result, map);
}

TEST_CASE_TEMPLATE("map with vector", T, std::multimap<int, std::vector<float>>, std::unordered_multimap<int, std::vector<float>>) {
    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);

    T map = {{1, {1.0f, 2.0f, 3.0f}}, {2, {4.0f, 5.0f, 6.0f}}, {3, {7.0f, 8.0f, 9.0f}}};
    REQUIRE(enc(map));

    auto dec = make_decoder(data);
    T    map_result;
    REQUIRE(dec(map_result));
    CHECK_EQ(map_result, map);
}

TEST_CASE_TEMPLATE("Multimap with variant of str and vector", T, std::multimap<std::string, std::variant<std::string, std::vector<float>>>,
                   std::unordered_multimap<std::string, std::variant<std::string, std::vector<float>>>) {

    constexpr auto no_ambigous_major_types_in_variant = valid_concept_mapping_v<std::variant<std::string, std::vector<float>>>;
    constexpr auto matching_major_types               = valid_concept_mapping_array_v<std::variant<std::string, std::vector<float>>>;
    fmt::print("no ambi: {}, {}\n", no_ambigous_major_types_in_variant, matching_major_types);

    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);

    T map = {{"one", "one"}, {"two", std::vector<float>{1.0f, 2.0f, 3.0f}}, {"three", "THE THREE"}};
    REQUIRE(enc(map));

    auto dec = make_decoder(data);
    T    map_result;
    auto status = dec(map_result);
    if (!status) {
        fmt::print("Error: {}\n", status_message(status.error()));
    }
    REQUIRE(status);
    CHECK_EQ(map_result, map);
}

TEST_CASE_TEMPLATE("Map with variant failing", T, std::map<int, std::variant<int, std::string>>,
                   std::unordered_map<int, std::variant<int, std::string>>) {
    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);

    std::unordered_map<int, std::variant<std::string, float>> map = {{1, "one"}, {2, 2.0f}, {3, "THE THREE"}};
    REQUIRE(enc(map));

    auto dec = make_decoder(data);
    T    map_result;
    auto status = dec(map_result);
    REQUIRE_FALSE(status);
    CHECK_EQ(status.error(), status_code::no_matching_tag_value_in_variant);
}