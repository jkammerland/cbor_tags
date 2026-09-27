#include "character_traits.h"
#include "test_util.h"

#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <span>
#include <vector>

using namespace cbor::tags;

TEST_SUITE("cbor_wire/byte_views") {
    TEST_CASE_TEMPLATE("byte views borrow the definite payload and advance", View, std::span<const std::byte>,
                       test_util::basic_string_view<std::byte>) {
        auto input = to_bytes("42616201");
        View value;
        auto dec = make_decoder(input);
        REQUIRE(dec(value));
        CHECK_EQ(value.size(), 2);
        CHECK(value.data() == input.data() + 1);
        std::uint8_t following{};
        REQUIRE(dec(following));
        CHECK_EQ(following, 1);
        CHECK(dec.tell() == input.end());
    }

    TEST_CASE_TEMPLATE("empty byte views advance to the following item or end", View, std::span<const std::byte>,
                       test_util::basic_string_view<std::byte>) {
        auto input         = to_bytes("40");
        bool has_following = false;
        SUBCASE("at end of input") {}
        SUBCASE("with following item") {
            input         = to_bytes("4001");
            has_following = true;
        }

        const std::array sentinel{std::byte{7}};
        View             value{sentinel.data(), sentinel.size()};
        auto             dec = make_decoder(input);
        REQUIRE(dec(value));
        CHECK(value.empty());
        if (has_following) {
            std::uint8_t following{};
            REQUIRE(dec(following));
            CHECK_EQ(following, 1);
        }
        CHECK(dec.tell() == input.end());
    }

    TEST_CASE_TEMPLATE("byte views reject unsupported headers without rebinding", View, std::span<const std::byte>,
                       test_util::basic_string_view<std::byte>) {
        std::vector<std::byte> input;
        SUBCASE("text string") { input = to_bytes("6161"); }
        SUBCASE("indefinite byte string") { input = to_bytes("5f4161ff"); }

        const std::array sentinel{std::byte{7}};
        View             value{sentinel.data(), sentinel.size()};
        auto             dec    = make_decoder(input);
        const auto       result = dec(value);
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_bstr_on_buffer);
        CHECK(value.data() == sentinel.data());
        CHECK_EQ(value.size(), 1);
    }

    TEST_CASE_TEMPLATE("byte views reject unavailable payloads without rebinding", View, std::span<const std::byte>,
                       test_util::basic_string_view<std::byte>) {
        std::vector<std::byte> input;
        bool                   oversized = false;
        SUBCASE("truncated payload") { input = to_bytes("4261"); }
        SUBCASE("oversized payload") {
            input     = to_bytes("5bffffffffffffffff61");
            oversized = true;
        }

        const std::array sentinel{std::byte{7}};
        View             value{sentinel.data(), sentinel.size()};
        auto             dec    = make_decoder(input);
        const auto       result = dec(value);
        REQUIRE_FALSE(result);
        if (oversized) {
            CHECK((result.error() == status_code::incomplete || result.error() == status_code::error));
        } else {
            CHECK_EQ(result.error(), status_code::incomplete);
        }
        CHECK(value.data() == sentinel.data());
        CHECK_EQ(value.size(), 1);
    }
}
