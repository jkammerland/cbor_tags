#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/float16_ieee754.h>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

using namespace cbor::tags;

namespace {

// Literal wire examples from RFC 8949 Appendix A, Table 6:
// https://www.rfc-editor.org/rfc/rfc8949.html#appendix-A
// The expected C++ value and wire bytes are independent inputs, not generated
// by this library. Indefinite containers normalize to the supplied definite form.
template <typename T> void check_vector(std::string_view hex, const T &expected, std::string_view encoded_hex = {}) {
    INFO("RFC 8949 Appendix A: ", hex);
    auto input = to_bytes(hex);
    auto dec   = make_decoder(input);
    T    decoded{};
    REQUIRE(dec(decoded));
    CHECK_EQ(dec.tell(), input.cend());
    CHECK(decoded == expected);

    std::vector<std::byte> output;
    REQUIRE(make_encoder(output)(expected));
    CHECK_EQ(to_hex(output), encoded_hex.empty() ? hex : encoded_hex);
}

template <typename T> void check_float(std::string_view hex, double expected) {
    INFO("RFC 8949 Appendix A float: ", hex);
    auto input = to_bytes(hex);
    auto dec   = make_decoder(input);
    T    decoded{};
    REQUIRE(dec(decoded));
    CHECK_EQ(dec.tell(), input.cend());
    const auto actual = static_cast<double>(decoded);
    if (std::isnan(expected)) {
        CHECK(std::isnan(actual));
    } else {
        CHECK_EQ(actual, expected);
        CHECK_EQ(std::signbit(actual), std::signbit(expected));
    }

    // Explicit source width preserves the RFC spelling, including signed zero.
    std::vector<std::byte> output;
    REQUIRE(make_encoder(output)(static_cast<T>(expected)));
    CHECK_EQ(to_hex(output), hex);
}

template <std::uint64_t Tag, typename T> void check_tag(std::string_view hex, T expected) {
    INFO("RFC 8949 Appendix A tag: ", hex);
    auto input = to_bytes(hex);
    auto dec   = make_decoder(input);
    auto value = make_tag_pair(static_tag<Tag>{}, T{});
    REQUIRE(dec(value));
    CHECK_EQ(dec.tell(), input.cend());
    CHECK(value.second == expected);
    std::vector<std::byte> output;
    REQUIRE(make_encoder(output)(make_tag_pair(static_tag<Tag>{}, expected)));
    CHECK_EQ(to_hex(output), hex);
}

} // namespace

TEST_CASE("rfc8949 appendix a integers") {
    const std::pair<std::string_view, std::uint64_t> positives[]{{"00", 0},
                                                                 {"01", 1},
                                                                 {"0a", 10},
                                                                 {"17", 23},
                                                                 {"1818", 24},
                                                                 {"1819", 25},
                                                                 {"1864", 100},
                                                                 {"1903e8", 1000},
                                                                 {"1a000f4240", 1000000},
                                                                 {"1b000000e8d4a51000", 1000000000000ULL},
                                                                 {"1bffffffffffffffff", UINT64_MAX}};
    for (const auto &[hex, value] : positives) {
        check_vector(hex, value);
    }
    const std::pair<std::string_view, std::int64_t> negatives[]{{"20", -1}, {"29", -10}, {"3863", -100}, {"3903e7", -1000}};
    for (const auto &[hex, value] : negatives) {
        check_vector(hex, value);
    }
    check_vector("3bffffffffffffffff", negative{0}); // -2^64: wrapper's zero sentinel.

    // Bignums are checked as tag + magnitude bytes, not narrowed native integers.
    check_tag<2>("c249010000000000000000", to_bytes("010000000000000000"));
    check_tag<3>("c349010000000000000000", to_bytes("010000000000000000"));
}

TEST_CASE("rfc8949 appendix a floating point") {
    const std::pair<std::string_view, double> halfs[]{{"f90000", 0.0},
                                                      {"f98000", -0.0},
                                                      {"f93c00", 1.0},
                                                      {"f93e00", 1.5},
                                                      {"f97bff", 65504.0},
                                                      {"f90001", 5.960464477539063e-8},
                                                      {"f90400", 0.00006103515625},
                                                      {"f9c400", -4.0},
                                                      {"f97c00", INFINITY},
                                                      {"f97e00", NAN},
                                                      {"f9fc00", -INFINITY}};
    for (const auto &[hex, value] : halfs) {
        check_float<float16_t>(hex, value);
    }
    const std::pair<std::string_view, double> singles[]{{"fa47c35000", 100000.0},
                                                        {"fa7f7fffff", 3.4028234663852886e+38},
                                                        {"fa7f800000", INFINITY},
                                                        {"fa7fc00000", NAN},
                                                        {"faff800000", -INFINITY}};
    for (const auto &[hex, value] : singles) {
        check_float<float>(hex, value);
    }
    const std::pair<std::string_view, double> doubles[]{{"fb3ff199999999999a", 1.1},  {"fb7e37e43c8800759c", 1.0e+300},
                                                        {"fbc010666666666666", -4.1}, {"fb7ff0000000000000", INFINITY},
                                                        {"fb7ff8000000000000", NAN},  {"fbfff0000000000000", -INFINITY}};
    for (const auto &[hex, value] : doubles) {
        check_float<double>(hex, value);
    }
}

TEST_CASE("rfc8949 appendix a simple values and tags") {
    check_vector("f4", false);
    check_vector("f5", true);
    check_vector("f6", nullptr);
    check_vector("f7", simple{23}); // undefined
    check_vector("f0", simple{16});
    check_vector("f8ff", simple{255});
    check_tag<0>("c074323031332d30332d32315432303a30343a30305a", std::string{"2013-03-21T20:04:00Z"});
    check_tag<1>("c11a514b67b0", std::uint64_t{1363896240});
    check_tag<1>("c1fb41d452d9ec200000", 1363896240.5);
    check_tag<23>("d74401020304", to_bytes("01020304"));
    check_tag<24>("d818456449455446", to_bytes("6449455446"));
    check_tag<32>("d82076687474703a2f2f7777772e6578616d706c652e636f6d", std::string{"http://www.example.com"});
}

TEST_CASE("rfc8949 appendix a strings") {
    check_vector("40", to_bytes(""));
    check_vector("4401020304", to_bytes("01020304"));
    const std::pair<std::string_view, std::string> texts[]{{"60", ""},
                                                           {"6161", "a"},
                                                           {"6449455446", "IETF"},
                                                           {"62225c", "\"\\"},
                                                           {"62c3bc", "\xc3\xbc"},
                                                           {"63e6b0b4", "\xe6\xb0\xb4"},
                                                           {"64f0908591", "\xf0\x90\x85\x91"}};
    for (const auto &[hex, value] : texts) {
        check_vector(hex, value);
    }
    check_vector("5f42010243030405ff", to_bytes("0102030405"), "450102030405");
    check_vector("7f657374726561646d696e67ff", std::string{"streaming"}, "6973747265616d696e67");
}

TEST_CASE("rfc8949 appendix a arrays") {
    check_vector("80", std::vector<int>{});
    check_vector("83010203", std::vector<int>{1, 2, 3});
    const std::vector<std::variant<int, std::vector<int>>> nested{1, std::vector<int>{2, 3}, std::vector<int>{4, 5}};
    check_vector("8301820203820405", nested);
    std::vector<int> sequence;
    for (int i = 1; i <= 25; ++i) {
        sequence.push_back(i);
    }
    constexpr std::string_view sequence_hex = "98190102030405060708090a0b0c0d0e0f101112131415161718181819";
    check_vector(sequence_hex, sequence);
    check_vector("9fff", std::vector<int>{}, "80");
    for (const auto hex : {"9f018202039f0405ffff", "9f01820203820405ff", "83018202039f0405ff", "83019f0203ff820405"}) {
        check_vector(hex, nested, "8301820203820405");
    }
    check_vector("9f0102030405060708090a0b0c0d0e0f101112131415161718181819ff", sequence, sequence_hex);
}

TEST_CASE("rfc8949 appendix a maps") {
    check_vector("a0", std::map<int, int>{});
    check_vector("a201020304", std::map<int, int>{{1, 2}, {3, 4}});
    const std::map<std::string, std::variant<int, std::vector<int>>> mixed{{"a", 1}, {"b", std::vector<int>{2, 3}}};
    check_vector("a26161016162820203", mixed);
    check_vector("bf61610161629f0203ffff", mixed, "a26161016162820203");
    const auto nested = std::tuple{std::string{"a"}, std::map<std::string, std::string>{{"b", "c"}}};
    check_vector("826161a161626163", nested);
    check_vector("826161bf61626163ff", nested, "826161a161626163");
    check_vector("a56161614161626142616361436164614461656145",
                 std::map<std::string, std::string>{{"a", "A"}, {"b", "B"}, {"c", "C"}, {"d", "D"}, {"e", "E"}});
    check_vector("bf6346756ef563416d7421ff", std::map<std::string, std::variant<bool, int>>{{"Fun", true}, {"Amt", -2}},
                 "a263416d74216346756ef5"); // std::map also orders keys.
}

TEST_CASE("rfc8949 appendix a truncated payloads are incomplete") {
    auto          integer_bytes = to_bytes("1bffffffffffffff");
    std::uint64_t integer_value{};
    CHECK_EQ(make_decoder(integer_bytes)(integer_value).error(), status_code::incomplete);
    auto        text_bytes = to_bytes("64f09085");
    std::string text;
    CHECK_EQ(make_decoder(text_bytes)(text).error(), status_code::incomplete);
    auto             array_bytes = to_bytes("9f0102");
    std::vector<int> array;
    CHECK_EQ(make_decoder(array_bytes)(array).error(), status_code::incomplete);
}
