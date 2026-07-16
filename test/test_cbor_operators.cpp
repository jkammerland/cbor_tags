#include "cbor_tags/cbor_operators.h"

#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <map>
#include <string>
#include <variant>
#include <vector>

using namespace cbor::tags;

TEST_CASE("variant comparator orders by variant index") {
    using variant_t = std::variant<std::uint64_t, std::string, std::nullptr_t>;

    CHECK(variant_comparator<>{}(variant_t{7U}, variant_t{std::string{"a"}}));
    CHECK_FALSE(variant_comparator<>{}(variant_t{std::string{"a"}}, variant_t{7U}));
    CHECK(variant_comparator<std::greater<>>{}(variant_t{std::string{"a"}}, variant_t{7U}));
}

TEST_CASE("variant comparator orders numeric and simple alternatives") {
    using variant_t = std::variant<std::uint64_t, bool, simple, std::nullptr_t>;

    CHECK(variant_comparator<>{}(variant_t{1U}, variant_t{2U}));
    CHECK_FALSE(variant_comparator<>{}(variant_t{2U}, variant_t{1U}));
    CHECK(variant_comparator<std::greater<>>{}(variant_t{2U}, variant_t{1U}));

    CHECK(variant_comparator<>{}(variant_t{false}, variant_t{true}));
    CHECK(variant_comparator<>{}(variant_t{simple{1}}, variant_t{simple{2}}));
    CHECK_FALSE(variant_comparator<>{}(variant_t{nullptr}, variant_t{nullptr}));
}

TEST_CASE("variant comparator orders string and container alternatives") {
    using bytes_t   = std::vector<std::byte>;
    using array_t   = std::vector<int>;
    using map_t     = std::map<int, int>;
    using variant_t = std::variant<std::string, bytes_t, array_t, map_t>;

    CHECK(variant_comparator<>{}(variant_t{std::string{"a"}}, variant_t{std::string{"b"}}));
    CHECK(variant_comparator<>{}(variant_t{std::string{"a"}}, variant_t{std::string{"aa"}}));
    CHECK(variant_comparator<std::greater<>>{}(variant_t{std::string{"b"}}, variant_t{std::string{"a"}}));

    CHECK(variant_comparator<>{}(variant_t{bytes_t{std::byte{0x01}}}, variant_t{bytes_t{std::byte{0x01}, std::byte{0x02}}}));
    CHECK(variant_comparator<>{}(variant_t{array_t{1, 2}}, variant_t{array_t{1, 3}}));
    CHECK(variant_comparator<>{}(variant_t{map_t{{1, 2}}}, variant_t{map_t{{1, 3}}}));
}

TEST_CASE("variant comparator handles tag alternatives") {
    using variant_t = std::variant<static_tag<1>, static_tag<2>>;

    CHECK(variant_comparator<>{}(variant_t{static_tag<1>{}}, variant_t{static_tag<2>{}}));
    CHECK_FALSE(variant_comparator<>{}(variant_t{static_tag<1>{}}, variant_t{static_tag<1>{}}));
}

TEST_CASE("variant visitor returns false for different direct types") {
    CHECK_FALSE(cbor_variant_visitor<std::less<>>{}(std::uint64_t{1}, std::string{"1"}));
}
