#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <ranges>
#include <string>
#include <vector>

using namespace cbor::tags;

TEST_SUITE("roundtrip/text_decode") {

    TEST_CASE_TEMPLATE("text decoding preserves values when the destination is cleared between items", Buffer, std::vector<std::byte>,
                       std::deque<std::byte>) {
        const std::string first(256, 'a');
        const std::string second(128, 'b');
        const std::string third{"a\0b", 3};
        Buffer            buffer;
        auto              enc = make_encoder(buffer);
        REQUIRE(enc(first, second, third));

        std::string decoded;
        decoded.reserve(first.size());
        auto dec = make_decoder(buffer);

        REQUIRE(dec(decoded));
        CHECK_EQ(decoded, first);

        decoded.clear();
        REQUIRE(dec(decoded));
        CHECK_EQ(decoded, second);

        decoded.clear();
        REQUIRE(dec(decoded));
        CHECK_EQ(decoded, third);
        CHECK(dec.tell() == std::ranges::end(buffer));
    }

    TEST_CASE_TEMPLATE("text decoding appends successive values and leaves a prefix unchanged for empty values", Buffer,
                       std::vector<std::byte>, std::deque<std::byte>) {
        const std::string first(128, 'a');
        const std::string empty;
        const std::string last{"suffix"};
        Buffer            buffer;
        auto              enc = make_encoder(buffer);
        REQUIRE(enc(first, empty, last));

        const std::string prefix{"prefix:"};
        std::string       decoded = prefix;
        decoded.reserve(prefix.size() + first.size() + last.size());
        auto dec = make_decoder(buffer);

        REQUIRE(dec(decoded));
        CHECK_EQ(decoded, prefix + first);
        REQUIRE(dec(decoded));
        CHECK_EQ(decoded, prefix + first);
        REQUIRE(dec(decoded));
        CHECK_EQ(decoded, prefix + first + last);
        CHECK(dec.tell() == std::ranges::end(buffer));
    }

    TEST_CASE_TEMPLATE("empty text decoding advances a cleared destination to the following item", Buffer, std::vector<std::byte>,
                       std::deque<std::byte>) {
        const std::string  empty;
        const std::string  next(256, 'x');
        const std::uint8_t sentinel = 42;
        Buffer             buffer;
        auto               enc = make_encoder(buffer);
        REQUIRE(enc(empty, next, sentinel));

        std::string decoded(512, 'p');
        decoded.clear();
        auto dec = make_decoder(buffer);

        REQUIRE(dec(decoded));
        CHECK(decoded.empty());
        REQUIRE(dec(decoded));
        CHECK_EQ(decoded, next);
        std::uint8_t decoded_sentinel{};
        REQUIRE(dec(decoded_sentinel));
        CHECK_EQ(decoded_sentinel, sentinel);
        CHECK(dec.tell() == std::ranges::end(buffer));
    }

} // TEST_SUITE("roundtrip/text_decode")
