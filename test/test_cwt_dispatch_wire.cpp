#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/codec/cwt.h"

#include <array>
#include <deque>
#include <doctest/doctest.h>

using namespace cbor::tags;

TEST_SUITE("cbor_wire/cwt") {
    TEST_CASE_TEMPLATE("cwt direct header dispatch returns terminal errors without replacing the destination", Header, cwt::header_map,
                       cwt::header_map_view) {
        struct fixture {
            std::vector<std::byte> bytes;
            status_code            expected;
        };
        const std::array cases{
            fixture{{std::byte{0xa1}, std::byte{0x01}}, status_code::incomplete},
            fixture{{std::byte{0xa1}, std::byte{0x01}, std::byte{0x38}}, status_code::incomplete},
            fixture{{std::byte{0xa1}, std::byte{0x01}, std::byte{0x40}}, status_code::error},
            fixture{{std::byte{0xbf}, std::byte{0x01}, std::byte{0x26}}, status_code::incomplete},
        };
        for (const auto &entry : cases) {
            CAPTURE(entry.bytes.size());
            CAPTURE(entry.expected);
            for (const bool header_already_read : {false, true}) {
                CAPTURE(header_already_read);
                Header value{};
                value.alg   = cwt::algorithm::es384;
                auto dec    = make_decoder<codec::cwt>(entry.bytes);
                auto status = status_code::success;
                if (header_already_read) {
                    const auto [major, info] = dec.read_initial_byte();
                    CHECK_NOTHROW(status = dec.decode(value, major, info));
                } else {
                    CHECK_NOTHROW(status = dec.decode(value));
                }
                CHECK(status == entry.expected);
                CHECK(value.alg == cwt::algorithm::es384);
                CHECK(dec.tell() == entry.bytes.end());
            }
        }
    }

    TEST_CASE("cwt direct header dispatch reports missing algorithm values on noncontiguous input") {
        const std::deque<std::byte> bytes{std::byte{0xa1}, std::byte{0x01}};
        cwt::header_map             value{.alg = cwt::algorithm::es384, .kid = std::nullopt, .crit = {}};
        auto                        dec    = make_decoder<codec::cwt>(bytes);
        auto                        status = status_code::success;
        CHECK_NOTHROW(status = dec.decode(value));
        CHECK(status == status_code::incomplete);
        CHECK(value.alg == cwt::algorithm::es384);
        CHECK(dec.tell() == bytes.end());
    }

    TEST_CASE_TEMPLATE("cwt direct header dispatch replaces the destination only after success", Header, cwt::header_map,
                       cwt::header_map_view) {
        const std::array bytes{std::byte{0xa1}, std::byte{0x01}, std::byte{0x26}, std::byte{0x09}};
        Header           value{};
        value.alg = cwt::algorithm::es384;
        value.crit.push_back(integer{1U});
        auto dec = make_decoder<codec::cwt>(bytes);
        CHECK(dec.decode(value) == status_code::success);
        CHECK(value.alg == cwt::algorithm::es256);
        CHECK(value.crit.empty());
        CHECK(dec.tell() == bytes.begin() + 3);
        int following{};
        REQUIRE(dec(following));
        CHECK(following == 9);
        CHECK(dec.tell() == bytes.end());
    }
}
