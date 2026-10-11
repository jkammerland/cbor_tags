#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/codec/cwt.h>
#include <cbor_tags/cwt/cwt.h>
#include <doctest/doctest.h>
#include <map>

using namespace cbor::tags;

namespace header_wire_test {
struct extra {
    std::optional<std::map<int, int>> claims;
};
auto cwt_fields(extra &value) { return cwt::fields(cwt::field<15>(value.claims)); }
auto cwt_fields(const extra &value) { return cwt::fields(cwt::field<15>(value.claims)); }
} // namespace header_wire_test

TEST_SUITE("cbor_wire/cwt") {
    TEST_CASE("type header accepts unsigned format or text media type") {
        for (const auto &input : {std::vector<std::byte>{std::byte{0xa1}, std::byte{0x10}, std::byte{0x18}, std::byte{0x3c}},
                                  std::vector<std::byte>{std::byte{0xa1}, std::byte{0x10}, std::byte{0x61}, std::byte{'x'}}}) {
            cwt::header_map header;
            REQUIRE(make_decoder<codec::cwt>(input)(header));
            CHECK(header.typ.has_value());
        }
    }

    TEST_CASE("type header rejects negative integers and byte strings") {
        for (const auto &input : {std::vector<std::byte>{std::byte{0xa1}, std::byte{0x10}, std::byte{0x20}},
                                  std::vector<std::byte>{std::byte{0xa1}, std::byte{0x10}, std::byte{0x40}}}) {
            cwt::header_map header;
            CHECK_FALSE(make_decoder<codec::cwt>(input)(header));
        }
    }

    TEST_CASE("claims header requires map values") {
        for (const auto &input : {std::vector<std::byte>{std::byte{0xa1}, std::byte{0x0f}, std::byte{0x80}},
                                  std::vector<std::byte>{std::byte{0xa1}, std::byte{0x0f}, std::byte{0x00}},
                                  std::vector<std::byte>{std::byte{0xa1}, std::byte{0x0f}, std::byte{0xf6}}}) {
            cwt::basic_header_map<false, header_wire_test::extra> header;
            CHECK_FALSE(make_decoder<codec::cwt>(input)(header));
        }

        std::vector<std::byte> input{std::byte{0xa1}, std::byte{0x0f}, std::byte{0xa1}, std::byte{0x01}, std::byte{0x07}};
        cwt::basic_header_map<false, header_wire_test::extra> header;
        REQUIRE(make_decoder<codec::cwt>(input)(header));
        REQUIRE(header.extra.claims);
        CHECK(header.extra.claims->at(1) == 7);
    }

    TEST_CASE("unknown extension duplicates are rejected") {
        std::vector<std::byte> input{std::byte{0xa2}, std::byte{0x18}, std::byte{0x2a}, std::byte{0x00},
                                     std::byte{0x18}, std::byte{0x2a}, std::byte{0x01}};
        cwt::header_map        header;
        CHECK_FALSE(make_decoder<codec::cwt>(input)(header));
    }
}

namespace empty_header_defaults {
struct extra {
    std::optional<int> value{3};
};
auto cwt_fields(extra &object) { return cwt::fields(cwt::field<"value">(object.value)); }
auto cwt_fields(const extra &object) { return cwt::fields(cwt::field<"value">(object.value)); }
} // namespace empty_header_defaults

TEST_CASE("empty protected encodings clear optional application defaults" * doctest::test_suite("cbor_wire/cwt")) {
    for (const auto &input : {std::vector<std::byte>{}, std::vector<std::byte>{std::byte{0xa0}}}) {
        cwt::basic_header_map<false, empty_header_defaults::extra> header;
        REQUIRE(header.extra.value);
        REQUIRE(cwt::decode_protected_header(input, header));
        CHECK_FALSE(header.extra.value);
    }
}
