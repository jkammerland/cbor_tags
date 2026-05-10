#include <cbor_tags/cbor_typed_arrays.h>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <span>
#include <vector>

TEST_CASE("typed arrays split header is directly usable") {
    std::vector<std::byte> encoded{std::byte{0xD8}, std::byte{0x4E}, std::byte{0x44}, std::byte{0x01},
                                   std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};

    auto view = cbor::tags::decode_rfc8746_typed_array_view<std::int32_t>(std::span<const std::byte>{encoded});

    REQUIRE(view);
    CHECK_EQ(view->size(), 1U);
    CHECK_EQ(view->copy_values()[0], 1);
}
