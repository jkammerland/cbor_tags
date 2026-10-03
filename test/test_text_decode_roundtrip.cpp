#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "test_util.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <ranges>
#include <string>
#include <vector>

using namespace cbor::tags;

TEST_SUITE("roundtrip/text_decode") {

    TEST_CASE_TEMPLATE("text decoding reuses reserved target storage across cleared and appended items", Buffer, std::vector<std::byte>,
                       std::deque<std::byte>) {
        const std::string  first(4096, 'a');
        const std::string  empty;
        const std::string  next(4096, 'b');
        const std::string  suffix{"x\0y", 3};
        const std::string  appended = next + suffix;
        const std::uint8_t sentinel = 42;
        Buffer             buffer;
        auto               enc = make_encoder(buffer);
        REQUIRE(enc(first, empty, next, suffix, sentinel));

        std::string decoded{"previous contents"};
        decoded.reserve(std::max(first.size(), appended.size()));
        const auto   retained_capacity = decoded.capacity();
        const auto  *retained_data     = decoded.data();
        auto         dec               = make_decoder(buffer);
        std::uint8_t decoded_sentinel{};

        const auto decode_without_allocation = [&](auto &value) {
            cbor::tags::test::detail::allocation_failure_guard guard;
            return dec(value);
        };

        decoded.clear();
        auto result = decode_without_allocation(decoded);
        REQUIRE_MESSAGE(result, status_message(result ? status_code::success : result.error()));
        CHECK_EQ(decoded, first);
        CHECK_EQ(decoded.capacity(), retained_capacity);
        CHECK(static_cast<const void *>(decoded.data()) == static_cast<const void *>(retained_data));

        decoded.clear();
        result = decode_without_allocation(decoded);
        REQUIRE_MESSAGE(result, status_message(result ? status_code::success : result.error()));
        CHECK_EQ(decoded, empty);
        CHECK_EQ(decoded.capacity(), retained_capacity);
        CHECK(static_cast<const void *>(decoded.data()) == static_cast<const void *>(retained_data));

        result = decode_without_allocation(decoded);
        REQUIRE_MESSAGE(result, status_message(result ? status_code::success : result.error()));
        CHECK_EQ(decoded, next);
        CHECK_EQ(decoded.capacity(), retained_capacity);
        CHECK(static_cast<const void *>(decoded.data()) == static_cast<const void *>(retained_data));

        result = decode_without_allocation(decoded);
        REQUIRE_MESSAGE(result, status_message(result ? status_code::success : result.error()));
        CHECK_EQ(decoded, appended);
        CHECK_EQ(decoded.capacity(), retained_capacity);
        CHECK(static_cast<const void *>(decoded.data()) == static_cast<const void *>(retained_data));

        result = decode_without_allocation(decoded_sentinel);
        REQUIRE_MESSAGE(result, status_message(result ? status_code::success : result.error()));
        CHECK_EQ(decoded_sentinel, sentinel);
        CHECK_EQ(decoded, appended);
        CHECK_EQ(decoded.capacity(), retained_capacity);
        CHECK(static_cast<const void *>(decoded.data()) == static_cast<const void *>(retained_data));
        CHECK(dec.tell() == std::ranges::end(buffer));
    }

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
