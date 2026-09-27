#include "character_traits.h"

#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <ranges>
#include <span>
#include <vector>

using namespace cbor::tags;

TEST_SUITE("cbor_roundtrip/byte_views") {
    TEST_CASE_TEMPLATE("byte views borrow typed payloads and consume following items", View, std::span<const std::byte>,
                       test_util::basic_string_view<std::byte>) {
        std::vector<std::byte> payload;
        SUBCASE("empty payload") {}
        SUBCASE("nonempty payload") { payload = {std::byte{1}, std::byte{2}, std::byte{3}}; }

        std::vector<std::byte> encoded;
        auto                   enc = make_encoder(encoded);
        REQUIRE(enc(payload, std::uint64_t{42}));

        View          value;
        std::uint64_t following{};
        auto          dec = make_decoder(encoded);
        REQUIRE(dec(value));
        CHECK(std::ranges::equal(value, payload));
        REQUIRE(dec(following));
        CHECK_EQ(following, 42);
        CHECK(dec.tell() == encoded.end());
    }

    TEST_CASE_TEMPLATE("empty byte views decode at the end of input", View, std::span<const std::byte>,
                       test_util::basic_string_view<std::byte>) {
        std::vector<std::byte> encoded;
        auto                   enc = make_encoder(encoded);
        REQUIRE(enc(std::vector<std::byte>{}));

        const std::array sentinel{std::byte{7}};
        View             value{sentinel.data(), sentinel.size()};
        auto             dec = make_decoder(encoded);
        REQUIRE(dec(value));
        CHECK(value.empty());
        CHECK(dec.tell() == encoded.end());
    }
}
