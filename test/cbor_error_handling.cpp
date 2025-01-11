#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <doctest/doctest.h>

using namespace cbor::tags;
using namespace cbor::tags::literals;
using namespace std::string_view_literals;

TEST_SUITE("Decoding the wrong thing") {
    TEST_CASE("Decode wrong tag") {
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        REQUIRE(enc(140_tag, "Hello world!"sv));

        auto        dec = make_decoder(data);
        std::string result;
        auto        result2 = dec(141_tag, result);
        CHECK(!result2);

        { /* Sanity check recovery - TODO: is this what we want? */
            auto result3 = dec(result);
            CHECK(result3);
            CHECK_EQ(result, "Hello world!");
        }
    }

    TEST_CASE_TEMPLATE("Decode wrong major types", T, std::string, std::vector<std::byte>, std::map<int, int>, static_tag<140>, float) {
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        REQUIRE(enc(int{140}));

        auto dec    = make_decoder(data);
        auto result = dec(T{});
        REQUIRE(!result);
        CHECK_EQ(result.error(), status::error);
    }
}