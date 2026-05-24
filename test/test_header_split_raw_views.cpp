#include <cbor_tags/cbor_raw_views.h>
#include <doctest/doctest.h>
#include <ranges>

using namespace cbor::tags;

TEST_CASE("raw views split header is directly usable") {
    auto bytes = encoded_byte_view{};

    static_assert(std::ranges::view<encoded_byte_view>);
    static_assert(std::ranges::sized_range<encoded_byte_view>);

    encoded_item_view  item{bytes};
    encoded_array_view array{bytes};
    encoded_map_view   map{bytes};

    CHECK(item.size() == 0);
    CHECK(array.size() == 0);
    CHECK(map.size() == 0);
}
