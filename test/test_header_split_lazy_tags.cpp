#include <cbor_tags/cbor_lazy_tags.h>

#include <cstddef>
#include <doctest/doctest.h>
#include <vector>

TEST_CASE("lazy tag public header is directly usable") {
    std::vector<std::byte> tagged{std::byte{0xC1}, std::byte{0x01}};
    auto                   tags = cbor::tags::find_tags<1>(tagged);
    auto                   it   = tags.begin();

    REQUIRE(it != tags.end());
    CHECK(it->tag() == 1);
    ++it;
    CHECK(it == tags.end());
    CHECK(tags.status() == cbor::tags::status_code::success);
}
