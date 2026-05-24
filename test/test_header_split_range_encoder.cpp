// Keep the split range header first so this test catches direct include dependencies.
// clang-format off
#include <cbor_tags/cbor_range_encoder.h>
#include <cbor_tags/cbor_encoder.h>
// clang-format on
#include <doctest/doctest.h>
#include <vector>

using namespace cbor::tags;

TEST_CASE("range encoder split header is directly includable") {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);

    REQUIRE(enc(as_array_range(std::vector<int>{1, 2})));
    CHECK(true);
}
