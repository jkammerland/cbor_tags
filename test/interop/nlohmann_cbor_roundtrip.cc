#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using json = nlohmann::json;

[[nodiscard]] std::vector<std::uint8_t> as_uint8(const std::vector<std::byte> &bytes) {
    auto result = std::vector<std::uint8_t>{};
    result.reserve(bytes.size());
    for (const auto byte : bytes) {
        result.push_back(static_cast<std::uint8_t>(byte));
    }
    return result;
}

[[nodiscard]] std::vector<std::byte> as_bytes(const std::vector<std::uint8_t> &bytes) {
    auto result = std::vector<std::byte>{};
    result.reserve(bytes.size());
    for (const auto byte : bytes) {
        result.push_back(static_cast<std::byte>(byte));
    }
    return result;
}

[[nodiscard]] std::vector<std::uint8_t> binary_bytes(const json::binary_t &binary) { return {binary.begin(), binary.end()}; }

} // namespace

TEST_CASE("nlohmann reads JSON-shaped CBOR emitted by cbor_tags") {
    using namespace cbor::tags;
    using namespace std::string_view_literals;

    auto output = std::vector<std::byte>{};
    auto enc    = make_encoder(output);

    REQUIRE(enc(as_map{5}, "name"sv, "Ada"sv, "age"sv, 42, "active"sv, true, "scores"sv, std::vector<int>{1, 2, 3}, "empty"sv, nullptr));

    const auto decoded = json::from_cbor(as_uint8(output));
    CHECK(decoded == json{{"active", true}, {"age", 42}, {"empty", nullptr}, {"name", "Ada"}, {"scores", {1, 2, 3}}});
}

TEST_CASE("cbor_tags reads JSON-shaped CBOR emitted by nlohmann") {
    using namespace cbor::tags;

    const auto input      = json{{"active", true}, {"age", 42}, {"empty", nullptr}, {"name", "Ada"}, {"scores", json::array({1, 2, 3})}};
    const auto input_cbor = json::to_cbor(input);
    const auto bytes      = as_bytes(input_cbor);

    using mapped_value = std::variant<bool, std::uint64_t, std::string, std::vector<std::uint64_t>, std::nullptr_t>;
    auto decoded       = std::map<std::string, mapped_value>{};
    auto dec           = make_decoder(bytes);

    REQUIRE(dec(decoded));
    REQUIRE(decoded.contains("active"));
    REQUIRE(decoded.contains("age"));
    REQUIRE(decoded.contains("empty"));
    REQUIRE(decoded.contains("name"));
    REQUIRE(decoded.contains("scores"));

    CHECK(std::get<bool>(decoded.at("active")));
    CHECK_EQ(std::get<std::uint64_t>(decoded.at("age")), 42U);
    CHECK(std::holds_alternative<std::nullptr_t>(decoded.at("empty")));
    CHECK_EQ(std::get<std::string>(decoded.at("name")), "Ada");
    CHECK_EQ(std::get<std::vector<std::uint64_t>>(decoded.at("scores")), std::vector<std::uint64_t>{1, 2, 3});
}

TEST_CASE("nlohmann and cbor_tags agree on CBOR byte strings") {
    using namespace cbor::tags;

    const auto binary = std::array<std::byte, 4>{std::byte{0x00}, std::byte{0x10}, std::byte{0x80}, std::byte{0xff}};

    auto output = std::vector<std::byte>{};
    auto enc    = make_encoder(output);
    REQUIRE(enc(binary));

    const auto nlohmann_decoded = json::from_cbor(as_uint8(output));
    REQUIRE(nlohmann_decoded.is_binary());
    CHECK(binary_bytes(nlohmann_decoded.get_binary()) == std::vector<std::uint8_t>{0x00, 0x10, 0x80, 0xff});

    const auto nlohmann_cbor = json::to_cbor(json::binary({0x01, 0x02, 0x03}));
    const auto input         = as_bytes(nlohmann_cbor);

    auto decoded = std::vector<std::byte>{};
    auto dec     = make_decoder(input);
    REQUIRE(dec(decoded));
    CHECK(decoded == std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}});
}

TEST_CASE("nlohmann tag handling is explicit for cbor_tags tagged byte strings") {
    using namespace cbor::tags;

    auto output = std::vector<std::byte>{};
    auto enc    = make_encoder(output);
    REQUIRE(enc(make_tag_pair(static_tag<42>{}, std::array<std::byte, 2>{std::byte{0xca}, std::byte{0xfe}})));

    const auto encoded                       = as_uint8(output);
    const auto parse_with_default_tag_policy = [&encoded] { [[maybe_unused]] const auto decoded = json::from_cbor(encoded); };
    CHECK_THROWS_AS(parse_with_default_tag_policy(), json::parse_error);

    const auto ignored_tag = json::from_cbor(encoded, true, true, json::cbor_tag_handler_t::ignore);
    REQUIRE(ignored_tag.is_binary());
    CHECK(binary_bytes(ignored_tag.get_binary()) == std::vector<std::uint8_t>{0xca, 0xfe});

    const auto stored_tag = json::from_cbor(encoded, true, true, json::cbor_tag_handler_t::store);
    REQUIRE(stored_tag.is_binary());
    REQUIRE(stored_tag.get_binary().has_subtype());
    CHECK_EQ(stored_tag.get_binary().subtype(), 42U);
    CHECK(binary_bytes(stored_tag.get_binary()) == std::vector<std::uint8_t>{0xca, 0xfe});
}

TEST_CASE("nlohmann rejects CBOR maps with non-text keys") {
    using namespace cbor::tags;
    using namespace std::string_view_literals;

    auto output = std::vector<std::byte>{};
    auto enc    = make_encoder(output);
    REQUIRE(enc(as_map{1}, 1U, "one"sv));

    const auto encoded                = as_uint8(output);
    const auto parse_non_text_key_map = [&encoded] { [[maybe_unused]] const auto decoded = json::from_cbor(encoded); };
    CHECK_THROWS_AS(parse_non_text_key_map(), json::parse_error);
}

TEST_CASE("nlohmann preserves cbor_tags unsigned integer boundary values") {
    using namespace cbor::tags;

    auto output = std::vector<std::byte>{};
    auto enc    = make_encoder(output);
    REQUIRE(enc(std::numeric_limits<std::uint64_t>::max()));

    const auto decoded = json::from_cbor(as_uint8(output));
    REQUIRE(decoded.is_number_unsigned());
    CHECK_EQ(decoded.get<std::uint64_t>(), std::numeric_limits<std::uint64_t>::max());
}
