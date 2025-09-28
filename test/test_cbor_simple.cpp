#include "cbor_tags/float16_ieee754.h"
#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <doctest/doctest.h>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <variant>

using namespace cbor::tags;

TEST_CASE("CBOR Encoder - Positive float") {
    std::vector<std::byte> data;
    auto                   enc   = make_encoder(data);
    float                  value = 3.14159f;
    CHECK(enc(value));
    CBOR_TAGS_TEST_LOG("Positive float: {}", to_hex(data));
    CHECK_EQ(to_hex(data), "fa40490fd0");

    auto  dec = make_decoder(data);
    float decoded;
    CHECK(dec(decoded));
    CHECK_EQ(value, decoded);
}

TEST_CASE("CBOR Encoder - Negative float") {
    std::vector<std::byte> data;
    auto                   enc   = make_encoder(data);
    float                  value = -3.14159f;
    CHECK(enc(value));
    CBOR_TAGS_TEST_LOG("Negative float: {}", to_hex(data));
    CHECK_EQ(to_hex(data), "fac0490fd0");
}

TEST_CASE("CBOR Encoder - Zero float") {
    std::vector<std::byte> data;
    auto                   enc   = make_encoder(data);
    float                  value = 0.0f;
    CHECK(enc(value));
    CBOR_TAGS_TEST_LOG("Zero: {}", to_hex(data));
    CHECK_EQ(to_hex(data), "fa00000000");
}

TEST_CASE("CBOR Encoder - Infinity float") {
    std::vector<std::byte> data;
    auto                   enc   = make_encoder(data);
    float                  value = std::numeric_limits<float>::infinity();
    CHECK(enc(value));
    CBOR_TAGS_TEST_LOG("Infinity: {}", to_hex(data));
    CHECK_EQ(to_hex(data), "fa7f800000");
}

TEST_CASE("CBOR Encoder - NaN float") {
    std::vector<std::byte> data;
    auto                   enc   = make_encoder(data);
    float                  value = std::numeric_limits<float>::quiet_NaN();
    CHECK(enc(value));
    CBOR_TAGS_TEST_LOG("NaN: {}", to_hex(data));
    CHECK_EQ(to_hex(data), "fa7fc00000");
}

TEST_CASE("CBOR Encoder - Positive double") {
    std::vector<std::byte> data;
    auto                   enc   = make_encoder(data);
    double                 value = 3.14159265358979323846;
    CHECK(enc(value));
    CBOR_TAGS_TEST_LOG("Positive double: {}", to_hex(data));
    CHECK_EQ(to_hex(data), "fb400921fb54442d18");
}

TEST_CASE("CBOR Encoder - Negative double") {
    std::vector<std::byte> data;
    auto                   enc   = make_encoder(data);
    double                 value = -3.14159265358979323846;
    CHECK(enc(value));
    CBOR_TAGS_TEST_LOG("Negative double: {}", to_hex(data));
    CHECK_EQ(to_hex(data), "fbc00921fb54442d18");
}

TEST_CASE("CBOR Encoder - Zero double") {
    std::vector<std::byte> data;
    auto                   enc   = make_encoder(data);
    double                 value = 0.0;
    CHECK(enc(value));
    CBOR_TAGS_TEST_LOG("Zero double: {}", to_hex(data));
    CHECK_EQ(to_hex(data), "fb0000000000000000");
}

TEST_CASE("CBOR Encoder - Infinity double") {
    std::vector<std::byte> data;
    auto                   enc   = make_encoder(data);
    double                 value = std::numeric_limits<double>::infinity();
    CHECK(enc(value));
    CBOR_TAGS_TEST_LOG("Infinity double: {}", to_hex(data));
    CHECK_EQ(to_hex(data), "fb7ff0000000000000");
}

TEST_CASE("CBOR Encoder - NaN double") {
    std::vector<std::byte> data;
    auto                   enc   = make_encoder(data);
    double                 value = std::numeric_limits<double>::quiet_NaN();
    CHECK(enc(value));
    CBOR_TAGS_TEST_LOG("NaN double: {}", to_hex(data));
    REQUIRE(data.size() == 9);
    CHECK(data[0] == static_cast<std::byte>(0xFB));
    CHECK((data[1] & static_cast<std::byte>(0x7F)) == static_cast<std::byte>(0x7F));
    CHECK((data[2] & static_cast<std::byte>(0xF0)) == static_cast<std::byte>(0xF0));
}

TEST_CASE("CBOR Encoder - simple") {
    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);
    simple                 number{19};
    CHECK(enc(number));
    CBOR_TAGS_TEST_LOG("Simple: {}", to_hex(data));
    CHECK_EQ(to_hex(data), "f3");

    auto   dec = make_decoder(data);
    simple decoded;
    REQUIRE(dec(decoded));
    CHECK_EQ(number, decoded);
}

TEST_CASE("CBOR check all simples") {
    { /* The special case when 1 extra byte is required */
        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);
        simple                 number{24};
        REQUIRE(enc(number));

        auto   dec = make_decoder(data);
        simple decoded;
        REQUIRE(dec(decoded));
        CHECK_EQ(number, decoded);
    }

    {
        for (int i = 0; i <= 255; i++) {
            std::vector<std::byte> data;

            auto   enc = make_encoder(data);
            simple number{static_cast<simple::value_type>(i)};
            REQUIRE(enc(number));

            auto   dec = make_decoder(data);
            simple decoded;
            REQUIRE(dec(decoded));
            CHECK_EQ(number, decoded);
        }
    }
}

TEST_CASE("Check simple status_code handling") {
    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);
    CHECK(enc(float16_t{3.14159f}));

    auto   dec = make_decoder(data);
    simple decoded;
    auto   result = dec(decoded);
    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::no_match_for_tag_simple_on_buffer);

    { /* Sanity check something that cannot be error handled */
        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);
        CHECK(enc(true));

        auto   dec = make_decoder(data);
        simple decoded;
        auto   result = dec(decoded);
        REQUIRE(result);
    }
}

TEST_CASE("Variant with simples") {
    std::vector<std::byte>                 data;
    auto                                   enc   = make_encoder(data);
    std::variant<double, float, float16_t> value = 3.14159f;

    REQUIRE(enc(value));

    CBOR_TAGS_TEST_LOG("Variant with simples: {}", to_hex(data));

    auto                                   dec = make_decoder(data);
    std::variant<double, float, float16_t> decoded;
    REQUIRE(dec(decoded));
    REQUIRE_EQ(value.index(), decoded.index());
    CHECK_EQ(std::get<float>(value), std::get<float>(decoded));
}

TEST_CASE("Test std::variant<int, double>") {
    auto buffer = std::vector<std::byte>{};
    auto enc    = make_encoder(buffer);

    std::variant<int, double> value{3.14};
    REQUIRE(enc(value));

    CBOR_TAGS_TEST_LOG("Variant with int and double: {}", to_hex(buffer));

    auto dec = make_decoder(buffer);

    std::variant<int, double> decoded;
    REQUIRE(dec(decoded));
    CHECK_EQ(value.index(), decoded.index());
    CHECK_EQ(std::get<double>(value), std::get<double>(decoded));
}