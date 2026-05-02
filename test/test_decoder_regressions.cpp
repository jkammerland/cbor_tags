#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <doctest/doctest.h>
#include <deque>
#include <vector>

using namespace cbor::tags;
using namespace std::string_view_literals;

namespace {
consteval bool negative_wrapper_argument_is_representable(std::uint64_t argument) {
    return argument != std::numeric_limits<std::uint64_t>::max();
}
} // namespace

static_assert(negative_wrapper_argument_is_representable(std::numeric_limits<std::uint64_t>::max() - 1));
static_assert(!negative_wrapper_argument_is_representable(std::numeric_limits<std::uint64_t>::max()));

TEST_CASE("decoder should reject unsigned integer overflow") {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1U));

    auto dec = make_decoder(buffer);

    std::uint32_t decoded{};
    auto          result = dec(decoded);

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::no_match_for_uint_on_buffer);
}

TEST_CASE("decoder should reject signed integer overflow") {
    std::vector<std::byte> buffer{std::byte{0x18}, std::byte{0x80}}; // uint(128)

    auto dec = make_decoder(buffer);

    std::int8_t decoded{};
    auto        result = dec(decoded);

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::no_match_for_int_on_buffer);
}

TEST_CASE("decoder should reject signed negative underflow") {
    std::vector<std::byte> buffer{std::byte{0x38}, std::byte{0x80}}; // -129

    auto dec = make_decoder(buffer);

    std::int8_t decoded{};
    auto        result = dec(decoded);

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::no_match_for_int_on_buffer);
}

TEST_CASE("decoder should accept integer decode boundaries") {
    {
        std::vector<std::byte> buffer{std::byte{0x38}, std::byte{0x7F}}; // -128
        auto                   dec = make_decoder(buffer);
        std::int8_t            decoded{};
        auto                   result = dec(decoded);

        REQUIRE(result);
        CHECK_EQ(decoded, std::numeric_limits<std::int8_t>::min());
    }

    {
        std::vector<std::byte> buffer{std::byte{0x18}, std::byte{0x7F}}; // uint(127)
        auto                   dec = make_decoder(buffer);
        std::int8_t            decoded{};
        auto                   result = dec(decoded);

        REQUIRE(result);
        CHECK_EQ(decoded, std::numeric_limits<std::int8_t>::max());
    }

    {
        std::vector<std::byte> buffer{std::byte{0x18}, std::byte{0xFF}}; // uint(255)
        auto                   dec = make_decoder(buffer);
        std::uint8_t           decoded{};
        auto                   result = dec(decoded);

        REQUIRE(result);
        CHECK_EQ(decoded, std::numeric_limits<std::uint8_t>::max());
    }
}

TEST_CASE("decoder should preserve cbor integer sign") {
    std::vector<std::byte> buffer{std::byte{0x20}}; // -1

    auto dec = make_decoder(buffer);

    integer decoded{0};
    auto    result = dec(decoded);

    REQUIRE(result);
    CHECK(decoded.is_negative);
    CHECK_EQ(decoded.value, 1);
}

TEST_CASE("decoder should document max negative wrapper edge behavior") {
    std::vector<std::byte> buffer{std::byte{0x3B}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
                                  std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}};

    {
        auto    dec = make_decoder(buffer);
        integer decoded{0};
        auto    result = dec(decoded);

        REQUIRE(result);
        CHECK(decoded.is_negative);
        CHECK_EQ(decoded.value, 0);
    }

    {
        auto     dec = make_decoder(buffer);
        negative decoded{1};
        auto     result = dec(decoded);

        REQUIRE(result);
        CHECK_EQ(decoded.value, 0);
    }
}

TEST_CASE("decoder should accept largest representable negative wrapper value") {
    std::vector<std::byte> buffer{std::byte{0x3B}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
                                  std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFE}};

    {
        auto    dec = make_decoder(buffer);
        integer decoded{0};
        auto    result = dec(decoded);

        REQUIRE(result);
        CHECK(decoded.is_negative);
        CHECK_EQ(decoded.value, std::numeric_limits<std::uint64_t>::max());
    }

    {
        auto     dec = make_decoder(buffer);
        negative decoded{1};
        auto     result = dec(decoded);

        REQUIRE(result);
        CHECK_EQ(decoded.value, std::numeric_limits<std::uint64_t>::max());
    }
}

TEST_CASE("decoder should accept empty byte strings") {
    std::vector<std::byte> buffer{std::byte{0x40}}; // 0-length bstr

    auto dec = make_decoder(buffer);

    std::vector<std::byte> decoded;
    auto                   result = dec(decoded);

    CHECK_MESSAGE(result, "Decoding an empty byte string should succeed.");
    CHECK(decoded.empty());
}

TEST_CASE("decoder should accept empty text strings") {
    std::vector<std::byte> buffer{std::byte{0x60}}; // 0-length tstr

    auto dec = make_decoder(buffer);

    std::string decoded;
    auto              result = dec(decoded);

    CHECK_MESSAGE(result, "Decoding an empty text string should succeed.");
    CHECK(decoded.empty());
}

TEST_CASE("decoder should preserve text bytes without utf8 validation") {
    // tstr(2): 0xC3 0x28 is an invalid UTF-8 sequence.
    std::vector<std::byte> buffer{std::byte{0x62}, std::byte{0xC3}, std::byte{0x28}, std::byte{0x01}};

    auto dec = make_decoder(buffer);

    std::string  decoded;
    std::uint8_t next_value{};
    auto         result = dec(decoded, next_value);

    std::string expected;
    expected.push_back(static_cast<char>(0xC3));
    expected.push_back(static_cast<char>(0x28));

    CHECK_MESSAGE(result, "Core text decode preserves bytes and does not validate UTF-8.");
    CHECK_EQ(decoded, expected);
    CHECK_EQ(next_value, 1);
}

TEST_CASE("decoder should reject undersized byte strings for fixed arrays") {
    std::vector<std::byte> buffer{std::byte{0x41}, std::byte{0x01}}; // length 1, value 0x01

    auto dec = make_decoder(buffer);

    std::array<std::byte, 2> decoded{};
    decoded.fill(std::byte{0xAA});

    auto result = dec(decoded);

    CHECK_FALSE_MESSAGE(result, "Decoding into a larger fixed array should flag the size mismatch.");
    CHECK_EQ(result.error(), status_code::unexpected_group_size);
    CHECK_EQ(decoded[0], std::byte{0xAA});
    CHECK_EQ(decoded[1], std::byte{0xAA});
}

TEST_CASE("decoder should decode byte strings into basic_string_view and advance") {
    // bstr(2): 0x42 0x01 0x02, then uint(1): 0x01
    std::vector<std::byte> buffer{std::byte{0x42}, std::byte{0x01}, std::byte{0x02}, std::byte{0x01}};

    auto dec = make_decoder(buffer);

    std::basic_string_view<std::byte> decoded;
    std::uint8_t                      next_value{};
    auto                              result = dec(decoded, next_value);

    CHECK_MESSAGE(result, "Decoding into a byte-string view should advance the reader.");
    CHECK_EQ(decoded.size(), 2);
    CHECK_EQ(decoded[0], std::byte{0x01});
    CHECK_EQ(decoded[1], std::byte{0x02});
    CHECK_EQ(next_value, 1);
}

TEST_CASE("decoder should accept empty byte strings for basic_string_view and advance") {
    // empty bstr: 0x40, then uint(1): 0x01
    std::vector<std::byte> buffer{std::byte{0x40}, std::byte{0x01}};

    auto dec = make_decoder(buffer);

    std::basic_string_view<std::byte> decoded;
    std::uint8_t                      next_value{};
    auto                              result = dec(decoded, next_value);

    CHECK_MESSAGE(result, "Decoding an empty byte-string view should succeed and advance the reader.");
    CHECK(decoded.empty());
    CHECK_EQ(next_value, 1);
}

TEST_CASE("decoder should accept empty byte strings for basic_string_view at end-of-buffer") {
    // empty bstr: 0x40
    // Regression: previously could form `&data_[pos]` out of bounds (ASAN).
    std::vector<std::byte> buffer{std::byte{0x40}};

    auto dec = make_decoder(buffer);

    std::basic_string_view<std::byte> decoded;
    auto                              result = dec(decoded);

    CHECK_MESSAGE(result, "Decoding an empty byte-string view should succeed without touching payload bytes.");
    CHECK(decoded.empty());
}

TEST_CASE("decoder should report incomplete byte-string view without retry contract") {
    // bstr(2): 0x42, but only 1 byte payload initially
    std::vector<std::byte> buffer{std::byte{0x42}, std::byte{0x01}};

    auto dec = make_decoder(buffer);

    std::basic_string_view<std::byte> decoded;
    auto                              result = dec(decoded);

    CHECK_FALSE_MESSAGE(result, "Truncated byte-string view should return incomplete.");
    CHECK_EQ(result.error(), status_code::incomplete);
}

TEST_CASE("decoder should report incomplete indefinite array without retry contract") {
    std::vector<std::byte> buffer{std::byte{0x9F}, std::byte{0x01}, std::byte{0x02}};

    auto dec = make_decoder(buffer);

    std::vector<int> decoded{99};
    auto             result = dec(decoded);

    CHECK_FALSE_MESSAGE(result, "Truncated indefinite array should return incomplete.");
    CHECK_EQ(result.error(), status_code::incomplete);
    CHECK_EQ(decoded.size(), 1);
    CHECK_EQ(decoded[0], 99);
}

TEST_CASE("decoder should report incomplete indefinite bstr without retry contract") {
    std::vector<std::byte> buffer{std::byte{0x5F}, std::byte{0x41}, std::byte{0xAA}};

    auto dec = make_decoder(buffer);

    std::vector<std::byte> decoded{std::byte{0xCC}};
    auto                   result = dec(decoded);

    CHECK_FALSE_MESSAGE(result, "Truncated indefinite bstr should return incomplete.");
    CHECK_EQ(result.error(), status_code::incomplete);
    CHECK_EQ(decoded.size(), 1);
    CHECK_EQ(decoded[0], std::byte{0xCC});
}

TEST_CASE("decoder should decode complete definite values in one shot") {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(std::vector<int>{1, 2, 3}, std::map<int, int>{{1, 2}}, std::string{"ok"}));

    auto dec = make_decoder(buffer);

    std::vector<int>  values;
    std::map<int, int> mapping;
    std::string       label;
    auto              result = dec(values, mapping, label);

    REQUIRE_MESSAGE(result, "Complete definite values should decode through the one-shot path.");
    CHECK_EQ(values, std::vector<int>{1, 2, 3});
    CHECK_EQ(mapping, (std::map<int, int>{{1, 2}}));
    CHECK_EQ(label, "ok");
}

TEST_CASE("decoder should decode complete non-contiguous definite and indefinite values in one shot") {
    std::deque<std::byte> buffer{
        std::byte{0x82}, std::byte{0x01}, std::byte{0x02},             // array [1, 2]
        std::byte{0xA1}, std::byte{0x01}, std::byte{0x02},             // map {1: 2}
        std::byte{0x9F}, std::byte{0x03}, std::byte{0x04}, std::byte{0xFF}, // indefinite array [3, 4]
        std::byte{0xBF}, std::byte{0x03}, std::byte{0x04}, std::byte{0xFF}, // indefinite map {3: 4}
        std::byte{0x5F}, std::byte{0x41}, std::byte{0xAA}, std::byte{0xFF}, // indefinite bstr h'AA'
        std::byte{0x7F}, std::byte{0x61}, std::byte{'x'}, std::byte{0xFF},  // indefinite tstr "x"
    };

    auto dec = make_decoder(buffer);

    std::vector<int>       definite_values;
    std::map<int, int>     definite_mapping;
    std::vector<int>       indefinite_values;
    std::map<int, int>     indefinite_mapping;
    std::vector<std::byte> bytes;
    std::string            text;
    auto                   result = dec(definite_values, definite_mapping, indefinite_values, indefinite_mapping, bytes, text);

    REQUIRE_MESSAGE(result, "Complete non-contiguous definite and indefinite values should decode in one shot.");
    CHECK_EQ(definite_values, std::vector<int>{1, 2});
    CHECK_EQ(definite_mapping, (std::map<int, int>{{1, 2}}));
    CHECK_EQ(indefinite_values, std::vector<int>{3, 4});
    CHECK_EQ(indefinite_mapping, (std::map<int, int>{{3, 4}}));
    CHECK_EQ(bytes, std::vector<std::byte>{std::byte{0xAA}});
    CHECK_EQ(text, "x");
}

TEST_CASE("decoder should validate as_text_any length against available bytes") {
    // tstr(5): 0x65, but only 3 bytes payload -> truncated
    std::vector<std::byte> buffer{std::byte{0x65}, std::byte{0x61}, std::byte{0x62}, std::byte{0x63}};

    auto dec = make_decoder(buffer);

    as_text_any header{};
    auto        result = dec(header);

    CHECK_FALSE_MESSAGE(result, "Decoding as_text_any on truncated input should fail.");
    CHECK_EQ(result.error(), status_code::incomplete);
}

TEST_CASE("decoder should not walk past end for as_text_any on non-contiguous truncated input") {
    // tstr(5): 0x65, but only 1 byte payload; decoding another item after skipping would be UB.
    std::deque<std::byte> buffer{std::byte{0x65}, std::byte{'a'}};

    auto dec = make_decoder(buffer);

    as_text_any header{};
    std::uint8_t next_value{};
    auto         result = dec(header, next_value);

    CHECK_FALSE_MESSAGE(result, "Truncated as_text_any must fail before attempting to advance non-contiguous iterators.");
    CHECK_EQ(result.error(), status_code::incomplete);
    CHECK_EQ(header.size, 5);
}

TEST_CASE("decoder should not advance non-contiguous iterators past end for as_bstr_any") {
    // bstr(1): 0x41 0xAA, then bstr(1): 0x41 but missing payload
    std::deque<std::uint8_t> buffer{0x41, 0xAA, 0x41};

    auto dec = make_decoder(buffer);

    as_bstr_any first{};
    as_bstr_any second{};
    auto        result = dec(first, second);

    CHECK_FALSE_MESSAGE(result, "Second as_bstr_any should detect incomplete payload without advancing past end.");
    CHECK_EQ(result.error(), status_code::incomplete);
    CHECK_EQ(first.size, 1);
    CHECK_EQ(second.size, 1);
}

TEST_CASE("decoder non-contiguous bstr_view should update offset for subsequent bounds checks") {
    // bstr(5): 0x45 01 02 03 04 05, then bstr(3): 0x43 AA (truncated payload)
    // Regression: if non-contiguous bstr decode doesn't keep current_offset_ in sync, the next header can skip past end (ASAN/crash).
    std::deque<std::byte> buffer{std::byte{0x45}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}, std::byte{0x05},
                                 std::byte{0x43}, std::byte{0xAA}};

    auto dec = make_decoder(buffer);

    decltype(dec)::bstr_view_t first_view{};
    as_bstr_any                second_header{};
    std::uint8_t               next_value{};
    auto                       result = dec(first_view, second_header, next_value);

    CHECK_FALSE_MESSAGE(result, "Second header should fail as incomplete without advancing beyond end.");
    CHECK_EQ(result.error(), status_code::incomplete);
    CHECK_EQ(second_header.size, 3);
}

TEST_CASE("decoder should reject array length mismatch for fixed-size containers") {
    // array(3): 0x83, items: 1,2,3
    std::vector<std::byte> buffer{std::byte{0x83}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    auto dec = make_decoder(buffer);

    std::array<int, 2> decoded{};
    decoded.fill(-1);
    auto result = dec(decoded);

    CHECK_FALSE_MESSAGE(result, "Decoding into a fixed-size container should validate array length.");
    CHECK_EQ(result.error(), status_code::unexpected_group_size);
    CHECK_EQ(decoded[0], -1);
    CHECK_EQ(decoded[1], -1);
}

TEST_CASE("decoder should reject array length mismatch for fixed-size spans") {
    // array(3): 0x83, items: 1,2,3
    // Regression: previously this could write past the end of the span (ASAN heap-buffer-overflow).
    std::vector<std::byte> buffer{std::byte{0x83}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};

    auto dec = make_decoder(buffer);

    std::vector<int> storage(2, -1);
    std::span<int>   decoded{storage};
    auto             result = dec(decoded);

    CHECK_FALSE_MESSAGE(result, "Decoding into a fixed-size span should validate array length.");
    CHECK_EQ(result.error(), status_code::unexpected_group_size);
    CHECK_EQ(storage[0], -1);
    CHECK_EQ(storage[1], -1);
}

TEST_CASE("encoder should not overflow fixed-size output buffers") {
    // Regression: previously, encoding would blindly write past the end of a fixed-size buffer (ASAN).
    std::array<std::byte, 0> buffer{};
    auto                     enc = make_encoder(buffer);

    auto result = enc(1u);

    CHECK_FALSE_MESSAGE(result, "Encoding into a zero-sized output buffer should fail safely.");
    CHECK_EQ(result.error(), status_code::error);
}

TEST_CASE("encoder should not overflow fixed-size output buffers for strings") {
    // Regression: previously, encoding a string payload into too-small output buffer could overflow (ASAN).
    std::array<std::byte, 1> buffer{};
    auto                     enc = make_encoder(buffer);

    auto result = enc("hello"sv);

    CHECK_FALSE_MESSAGE(result, "Encoding into a too-small fixed buffer should fail safely.");
    CHECK_EQ(result.error(), status_code::error);
}
