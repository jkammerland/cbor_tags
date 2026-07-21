#include <cbor_tags/cbor_variant_traits.h>
#include <cstddef>
#include <doctest/doctest.h>
#include <string>
#include <type_traits>
#include <variant>

TEST_CASE("variant traits public header is directly usable") {
    using variant_type = std::variant<int, std::string>;
    using traits       = cbor::tags::variant_traits<variant_type>;

    static_assert(traits::size == 2U);
    static_assert(std::is_same_v<traits::alternative<0>, int>);

    variant_type value{std::string{"text"}};
    CHECK(traits::index(value) == 1U);
    CHECK(traits::get<1>(value) == "text");
}
