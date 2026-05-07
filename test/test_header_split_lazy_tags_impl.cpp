#include <cbor_tags/cbor_lazy_tags_impl.h>
#include <cstddef>
#include <doctest/doctest.h>
#include <vector>

TEST_CASE("lazy tag implementation header is directly includable") {
    std::vector<std::byte> tagged{std::byte{0xC1}, std::byte{0x01}};
    auto                   tags = cbor::tags::find_tags<1>(tagged);

    CHECK(tags.begin() != tags.end());
}
