#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/float16_ieee754.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>
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

template <typename... Args> [[nodiscard]] std::vector<std::byte> encode_with_cbor_tags(Args &&...args) {
    auto output = std::vector<std::byte>{};
    auto enc    = cbor::tags::make_encoder(output);
    auto result = enc(std::forward<Args>(args)...);
    REQUIRE(result);
    return output;
}

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

    auto output = encode_with_cbor_tags(make_tag_pair(static_tag<42>{}, std::array<std::byte, 2>{std::byte{0xca}, std::byte{0xfe}}));

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

TEST_CASE("nlohmann tag handling is explicit for cbor_tags tagged text strings") {
    using namespace cbor::tags;
    using namespace std::string_view_literals;

    const auto output  = encode_with_cbor_tags(make_tag_pair(static_tag<42>{}, "Ada"sv));
    const auto encoded = as_uint8(output);

    CHECK_THROWS_AS(([&encoded] { [[maybe_unused]] const auto decoded = json::from_cbor(encoded); }()), json::parse_error);

    const auto ignored_tag = json::from_cbor(encoded, true, true, json::cbor_tag_handler_t::ignore);
    REQUIRE(ignored_tag.is_string());
    CHECK_EQ(ignored_tag.get<std::string>(), "Ada");

    const auto store_tagged_text = [&encoded] {
        [[maybe_unused]] const auto decoded = json::from_cbor(encoded, true, true, json::cbor_tag_handler_t::store);
    };
    CHECK_THROWS_AS(store_tagged_text(), json::parse_error);
}

TEST_CASE("cbor_tags reads nlohmann binary subtypes as tagged byte strings") {
    using namespace cbor::tags;

    const auto nlohmann_cbor = json::to_cbor(json::binary({0xca, 0xfe}, 42U));
    const auto input         = as_bytes(nlohmann_cbor);

    auto plain_bytes = std::vector<std::byte>{};
    auto plain_dec   = make_decoder(input);
    CHECK_FALSE(plain_dec(plain_bytes));

    auto tagged_bytes = make_tag_pair(static_tag<42>{}, std::vector<std::byte>{});
    auto tagged_dec   = make_decoder(input);
    REQUIRE(tagged_dec(tagged_bytes));
    CHECK(tagged_bytes.second == std::vector<std::byte>{std::byte{0xca}, std::byte{0xfe}});
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

TEST_CASE("cbor_tags reads non-text map keys that nlohmann rejects") {
    using namespace cbor::tags;
    using namespace std::string_view_literals;

    const auto binary_key = std::array<std::byte, 2>{std::byte{0xca}, std::byte{0xfe}};
    const auto output     = encode_with_cbor_tags(as_map{6}, 1U, "uint"sv, negative{1}, "negative"sv, true, "bool"sv, nullptr, "null"sv,
                                                  binary_key, "bytes"sv, wrap_as_array{1U, 2U}, "array"sv);

    const auto encoded                = as_uint8(output);
    const auto parse_non_text_key_map = [&encoded] { [[maybe_unused]] const auto decoded = json::from_cbor(encoded); };
    CHECK_THROWS_AS(parse_non_text_key_map(), json::parse_error);

    auto dec = make_decoder(output);
    REQUIRE(dec(as_map{6}));

    auto uint_key = std::uint64_t{};
    auto value    = std::string{};
    REQUIRE(dec(uint_key, value));
    CHECK_EQ(uint_key, 1U);
    CHECK_EQ(value, "uint");

    auto negative_key = negative{};
    REQUIRE(dec(negative_key, value));
    CHECK_EQ(negative_key, negative{1});
    CHECK_EQ(value, "negative");

    auto bool_key = false;
    REQUIRE(dec(bool_key, value));
    CHECK(bool_key);
    CHECK_EQ(value, "bool");

    auto null_key = nullptr;
    REQUIRE(dec(null_key, value));
    CHECK_EQ(value, "null");

    auto decoded_binary_key = std::vector<std::byte>{};
    REQUIRE(dec(decoded_binary_key, value));
    CHECK(decoded_binary_key == std::vector<std::byte>{std::byte{0xca}, std::byte{0xfe}});
    CHECK_EQ(value, "bytes");

    auto array_key = std::vector<std::uint64_t>{};
    REQUIRE(dec(array_key, value));
    CHECK(array_key == std::vector<std::uint64_t>{1U, 2U});
    CHECK_EQ(value, "array");
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

TEST_CASE("nlohmann signed integer boundary behavior is documented") {
    using namespace cbor::tags;

    const auto signed_boundaries =
        encode_with_cbor_tags(wrap_as_array{-1, -24, -25, std::numeric_limits<std::int64_t>::min(), 0, 1, 23, 24});
    const auto decoded_boundaries = json::from_cbor(as_uint8(signed_boundaries));
    REQUIRE(decoded_boundaries.is_array());
    REQUIRE_EQ(decoded_boundaries.size(), 8U);
    CHECK_EQ(decoded_boundaries[0].get<std::int64_t>(), -1);
    CHECK_EQ(decoded_boundaries[1].get<std::int64_t>(), -24);
    CHECK_EQ(decoded_boundaries[2].get<std::int64_t>(), -25);
    CHECK_EQ(decoded_boundaries[3].get<std::int64_t>(), std::numeric_limits<std::int64_t>::min());
    CHECK_EQ(decoded_boundaries[4].get<std::int64_t>(), 0);
    CHECK_EQ(decoded_boundaries[5].get<std::int64_t>(), 1);
    CHECK_EQ(decoded_boundaries[6].get<std::int64_t>(), 23);
    CHECK_EQ(decoded_boundaries[7].get<std::int64_t>(), 24);

    const auto below_int64_min = encode_with_cbor_tags(negative{(std::uint64_t{1} << 63U) + 1U});
    const auto decoded         = json::from_cbor(as_uint8(below_int64_min));
    REQUIRE(decoded.is_number_integer());
    CHECK_EQ(decoded.get<std::int64_t>(), std::numeric_limits<std::int64_t>::max());
}

TEST_CASE("nlohmann reads cbor_tags finite and non-finite floats") {
    using namespace cbor::tags;

    const auto half = json::from_cbor(as_uint8(encode_with_cbor_tags(float16_t{1.5F})));
    REQUIRE(half.is_number_float());
    CHECK_EQ(half.get<double>(), doctest::Approx(1.5));

    const auto single = json::from_cbor(as_uint8(encode_with_cbor_tags(3.25F)));
    REQUIRE(single.is_number_float());
    CHECK_EQ(single.get<double>(), doctest::Approx(3.25));

    const auto negative_zero = json::from_cbor(as_uint8(encode_with_cbor_tags(-0.0)));
    REQUIRE(negative_zero.is_number_float());
    CHECK(std::signbit(negative_zero.get<double>()));

    const auto infinity = json::from_cbor(as_uint8(encode_with_cbor_tags(std::numeric_limits<double>::infinity())));
    REQUIRE(infinity.is_number_float());
    CHECK(std::isinf(infinity.get<double>()));
    CHECK_FALSE(std::signbit(infinity.get<double>()));

    const auto negative_infinity = json::from_cbor(as_uint8(encode_with_cbor_tags(-std::numeric_limits<double>::infinity())));
    REQUIRE(negative_infinity.is_number_float());
    CHECK(std::isinf(negative_infinity.get<double>()));
    CHECK(std::signbit(negative_infinity.get<double>()));

    const auto nan = json::from_cbor(as_uint8(encode_with_cbor_tags(std::numeric_limits<double>::quiet_NaN())));
    REQUIRE(nan.is_number_float());
    CHECK(std::isnan(nan.get<double>()));
}

TEST_CASE("nlohmann rejects cbor_tags simple values beyond JSON null and bool") {
    using namespace cbor::tags;

    for (const auto value : {simple{16}, simple{23}, simple{24}, simple{255}}) {
        const auto output         = encode_with_cbor_tags(value);
        const auto parse_as_json  = [&output] { [[maybe_unused]] const auto decoded = json::from_cbor(as_uint8(output)); };
        auto       decoded_simple = simple{};
        auto       dec            = make_decoder(output);

        CHECK_THROWS_AS(parse_as_json(), json::parse_error);
        REQUIRE(dec(decoded_simple));
        CHECK_EQ(decoded_simple, value);
    }
}

TEST_CASE("nlohmann collapses duplicate text keys in CBOR maps") {
    using namespace cbor::tags;
    using namespace std::string_view_literals;

    const auto output  = encode_with_cbor_tags(as_map{2}, "x"sv, 1U, "x"sv, 2U);
    const auto decoded = json::from_cbor(as_uint8(output));

    REQUIRE(decoded.is_object());
    CHECK_EQ(decoded.size(), 1U);
    CHECK_EQ(decoded.at("x").get<std::uint64_t>(), 2U);
}

TEST_CASE("nlohmann accepts cbor_tags valid indefinite JSON-compatible forms") {
    using namespace cbor::tags;

    const auto array_values = std::vector<std::uint64_t>{1U, 2U, 3U};
    const auto array_json   = json::from_cbor(as_uint8(encode_with_cbor_tags(as_indefinite{array_values})));
    CHECK(array_json == json::array({1, 2, 3}));

    const auto map_values = std::map<std::string, std::uint64_t>{{"a", 1U}, {"b", 2U}};
    const auto map_json   = json::from_cbor(as_uint8(encode_with_cbor_tags(as_indefinite{map_values})));
    CHECK(map_json == json{{"a", 1}, {"b", 2}});

    const auto text      = std::string{"Ada"};
    const auto text_json = json::from_cbor(as_uint8(encode_with_cbor_tags(as_indefinite{text})));
    REQUIRE(text_json.is_string());
    CHECK_EQ(text_json.get<std::string>(), text);

    const auto bytes      = std::vector<std::byte>{std::byte{0xca}, std::byte{0xfe}};
    const auto bytes_json = json::from_cbor(as_uint8(encode_with_cbor_tags(as_indefinite{bytes})));
    REQUIRE(bytes_json.is_binary());
    CHECK(binary_bytes(bytes_json.get_binary()) == std::vector<std::uint8_t>{0xca, 0xfe});
}
