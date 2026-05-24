#include <cbor_tags/cbor_ranges.h>
#include <doctest/doctest.h>
#include <ranges>
#include <utility>
#include <vector>

using namespace cbor::tags;

TEST_CASE("range split header is directly usable") {
    std::vector<int> values{1, 2, 3};
    auto             array = as_array_range(values);
    static_assert(std::ranges::view<decltype(array.range_)>);

    std::vector<std::pair<int, int>> pairs{{1, 2}};
    auto                             map = as_map_range(pairs);
    static_assert(std::ranges::view<decltype(map.range_)>);

    std::vector<unsigned char> bytes{1, 2};
    auto                       bstr = as_bstr_range(bytes);
    static_assert(std::ranges::view<decltype(bstr.range_)>);

    CHECK(true);
}
