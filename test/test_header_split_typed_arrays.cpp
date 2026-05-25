// A public extension header should be enough for its codec wrappers and factories.
#include <cbor_tags/extensions/rfc8746_typed_arrays.h>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <vector>

using namespace cbor::tags;
using namespace cbor::tags::ext::rfc8746;

TEST_CASE("typed arrays split header is directly usable") {
    std::vector<std::byte> encoded{std::byte{0xD8}, std::byte{0x4E}, std::byte{0x44}, std::byte{0x01},
                                   std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};

    typed_array_view<std::int32_t> view;
    auto                           dec = make_decoder<typed_array_codec>(encoded);

    REQUIRE(dec(view));
    CHECK_EQ(view.size(), 1U);
    CHECK_EQ(view.copy_values()[0], 1);
}
