#include "test_util.h"

#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_ranges.h>
#include <cstddef>
#include <deque>
#include <doctest/doctest.h>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

using namespace cbor::tags;

TEST_CASE("dynamic bounded borrowed string views decode definite payloads without ownership") {
    SUBCASE("empty text view") {
        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);
        REQUIRE(enc(std::string{}));

        std::string_view value{"before"};
        auto             dec = make_decoder(buffer);
        REQUIRE(dec(as_bounded_size(value, 0, 0)));
        CHECK(value.empty());
    }

    SUBCASE("text view at maximum size") {
        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);
        REQUIRE(enc(std::string{"name"}));

        std::string_view value{"before"};
        auto             dec = make_decoder(buffer);
        REQUIRE(dec(as_bounded_size(value, 1, 4)));

        CHECK_EQ(value, "name");
        CHECK(reinterpret_cast<const std::byte *>(value.data()) == buffer.data() + 1);
    }

    SUBCASE("binary view at maximum size") {
        const std::vector<std::byte> payload{std::byte{0xaa}, std::byte{0xbb}};
        std::vector<std::byte>       buffer;
        auto                         enc = make_encoder(buffer);
        REQUIRE(enc(payload));

        const std::array<std::byte, 1>    sentinel{std::byte{0xcc}};
        std::basic_string_view<std::byte> value{sentinel.data(), sentinel.size()};
        auto                              dec = make_decoder(buffer);
        REQUIRE(dec(as_bounded_size(value, 0, 2)));

        CHECK_EQ(value.size(), payload.size());
        CHECK(std::ranges::equal(value, payload));
        CHECK(value.data() == buffer.data() + 1);
    }
}

TEST_CASE("dynamic bounded borrowed string views reject invalid wire shapes without rebinding") {
    SUBCASE("minimum size") {
        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);
        REQUIRE(enc(std::string{}));

        std::string_view value{"before"};
        auto             dec    = make_decoder(buffer);
        auto             result = dec(as_bounded_size(value, 1, 4));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(value, "before");
    }

    SUBCASE("maximum size") {
        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);
        REQUIRE(enc(std::string{"names"}));

        std::string_view value{"before"};
        auto             dec    = make_decoder(buffer);
        auto             result = dec(as_bounded_size(value, 1, 4));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK_EQ(value, "before");
    }

    SUBCASE("indefinite text") {
        auto             buffer = to_bytes("7f6161ff");
        std::string_view value{"before"};
        auto             dec    = make_decoder(buffer);
        auto             result = dec(as_bounded_size(value, 0, 4));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_tstr_on_buffer);
        CHECK_EQ(value, "before");
    }

    SUBCASE("indefinite binary") {
        auto                              buffer = to_bytes("5f41aaff");
        const std::array<std::byte, 1>    sentinel{std::byte{0xcc}};
        std::basic_string_view<std::byte> value{sentinel.data(), sentinel.size()};
        auto                              dec    = make_decoder(buffer);
        auto                              result = dec(as_bounded_size(value, 0, 4));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_bstr_on_buffer);
        CHECK(value.data() == sentinel.data());
        CHECK_EQ(value.size(), sentinel.size());
    }

    SUBCASE("non-contiguous input") {
        std::vector<std::byte> encoded;
        auto                   enc = make_encoder(encoded);
        REQUIRE(enc(std::string{"name"}));
        std::deque<std::byte> input(encoded.begin(), encoded.end());

        std::string_view value{"before"};
        auto             dec    = make_decoder(input);
        auto             result = dec(as_bounded_size(value, 1, 4));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::contiguous_view_on_non_contiguous_data);
        CHECK_EQ(value, "before");
    }
}
