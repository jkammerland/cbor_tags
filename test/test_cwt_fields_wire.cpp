#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/codec/cwt.h>
#include <doctest/doctest.h>

using namespace cbor::tags;

TEST_SUITE("cbor_wire/cwt") {
    TEST_CASE("custom field direct dispatch returns incomplete for truncated values") {
        std::vector<std::byte> input{std::byte{0xa1}, std::byte{0x01}, std::byte{0x19}, std::byte{0x00}};
        unsigned               value{};
        auto                   map     = cwt::fields(cwt::field<1>(value));
        auto                   decoder = make_decoder<codec::cwt>(input);
        auto [major, info]             = decoder.read_initial_byte();
        auto status                    = status_code::success;
        CHECK_NOTHROW(status = decoder.decode(map, major, info));
        CHECK(status == status_code::incomplete);
    }

    TEST_CASE("custom fields produce one map and omit absent optionals") {
        int                    value = 9;
        std::optional<int>     absent;
        auto                   map = cwt::fields(cwt::field<-1>(value), cwt::field<"absent">(absent));
        std::vector<std::byte> buffer;
        REQUIRE(make_encoder<codec::cwt>(buffer)(map));
        CHECK(buffer == std::vector<std::byte>{std::byte{0xa1}, std::byte{0x20}, std::byte{0x09}});
    }

    TEST_CASE("custom fields reject duplicate known and unknown labels") {
        for (const auto input :
             {std::vector<std::byte>{std::byte{0xa2}, std::byte{0x01}, std::byte{0x00}, std::byte{0x18}, std::byte{0x01}, std::byte{0x00}},
              std::vector<std::byte>{std::byte{0xa2}, std::byte{0x02}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00}},
              std::vector<std::byte>{std::byte{0xa2}, std::byte{0x61}, std::byte{'x'}, std::byte{0x00}, std::byte{0x61}, std::byte{'x'},
                                     std::byte{0x00}}}) {
            std::optional<int> value;
            auto               map = cwt::fields(cwt::field<1>(value));
            REQUIRE_FALSE(make_decoder<codec::cwt>(input)(map));
        }
    }

    TEST_CASE("custom fields reject absent required fields and explicit null") {
        SUBCASE("required field absent") {
            std::vector<std::byte> input{std::byte{0xa0}};
            int                    value{};
            auto                   map = cwt::fields(cwt::field<1>(value));
            CHECK_FALSE(make_decoder<codec::cwt>(input)(map));
        }
        SUBCASE("optional int is not nullable") {
            std::vector<std::byte> input{std::byte{0xa1}, std::byte{0x01}, std::byte{0xf6}};
            std::optional<int>     value;
            auto                   map = cwt::fields(cwt::field<1>(value));
            CHECK_FALSE(make_decoder<codec::cwt>(input)(map));
        }
    }

    TEST_CASE("custom fields accept indefinite maps and retain terminal prefixes") {
        std::vector<std::byte> input{std::byte{0xbf}, std::byte{0x01}, std::byte{0x07}, std::byte{0xff}};
        int                    value{};
        auto                   map = cwt::fields(cwt::field<1>(value));
        REQUIRE(make_decoder<codec::cwt>(input)(map));
        CHECK(value == 7);

        input              = {std::byte{0xa2}, std::byte{0x01}, std::byte{0x08}, std::byte{0x02}};
        auto       decoder = make_decoder<codec::cwt>(input);
        const auto result  = decoder(map);
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::incomplete);
        CHECK(value == 8);
        CHECK(decoder.tell() == input.end());
    }
}
