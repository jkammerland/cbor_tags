#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <doctest/doctest.h>
#include <span>
#include <string_view>
#include <vector>

using namespace cbor::tags;

TEST_SUITE("cbor_wire/container_interoperability") {
    TEST_CASE("arrays retain text views into definite and indefinite input") {
        for (const auto *hex : {"826161616207", "9f61616162ff07"}) {
            CAPTURE(hex);
            const auto                    bytes = to_bytes(hex);
            std::vector<std::string_view> decoded;
            auto                          dec = make_decoder(bytes);
            REQUIRE(dec(decoded));
            REQUIRE(decoded.size() == 2);
            CHECK(decoded[0] == "a");
            CHECK(decoded[1] == "b");
            CHECK(decoded[0].data() == reinterpret_cast<const char *>(bytes.data() + 2));
            CHECK(decoded[1].data() == reinterpret_cast<const char *>(bytes.data() + 4));
            int following{};
            REQUIRE(dec(following));
            CHECK(following == 7);
            CHECK(dec.tell() == bytes.end());
        }
    }

    TEST_CASE("arrays retain byte spans into definite and indefinite input") {
        for (const auto *hex : {"824101410207", "9f41014102ff07"}) {
            CAPTURE(hex);
            const auto                              bytes = to_bytes(hex);
            std::vector<std::span<const std::byte>> decoded;
            auto                                    dec = make_decoder(bytes);
            REQUIRE(dec(decoded));
            REQUIRE(decoded.size() == 2);
            CHECK(decoded[0].size() == 1);
            CHECK(decoded[1].size() == 1);
            CHECK(decoded[0].data() == bytes.data() + 2);
            CHECK(decoded[1].data() == bytes.data() + 4);
            int following{};
            REQUIRE(dec(following));
            CHECK(following == 7);
            CHECK(dec.tell() == bytes.end());
        }
    }
}
