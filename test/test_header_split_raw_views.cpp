#include <cbor_tags/cbor_raw_views.h>
#include <doctest/doctest.h>
#include <ranges>

TEST_CASE("raw views split header is directly usable") {
    auto bytes = cbor::tags::encoded_byte_view{};

    static_assert(std::ranges::view<cbor::tags::encoded_byte_view>);
    static_assert(std::ranges::sized_range<cbor::tags::encoded_byte_view>);

    cbor::tags::encoded_item_view  item{bytes};
    cbor::tags::encoded_array_view array{bytes};
    cbor::tags::encoded_map_view   map{bytes};

    CHECK(item.size() == 0);
    CHECK(array.size() == 0);
    CHECK(map.size() == 0);
}
