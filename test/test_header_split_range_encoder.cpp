#include <cbor_tags/cbor_range_encoder.h>

#include <cbor_tags/cbor_encoder.h>
#include <doctest/doctest.h>
#include <vector>

TEST_CASE("range encoder split header is directly includable") {
    std::vector<std::byte> buffer;
    auto                   enc = cbor::tags::make_encoder(buffer);

    REQUIRE(enc(cbor::tags::as_array_range(std::vector<int>{1, 2})));
    CHECK(true);
}
