#include <cbor_tags/cbor_segments.h>
#include <doctest/doctest.h>
#include <span>
#include <vector>

TEST_CASE("segments split header is directly usable") {
    std::vector<std::byte> payload{std::byte{0x01}, std::byte{0x02}};
    auto                   segments = cbor::tags::encode_bstr_segments(std::span<const std::byte>{payload});

    CHECK_EQ(segments.size(), 2U);
    CHECK(segments[0].is_owned());
    CHECK(segments[1].is_borrowed());
}
