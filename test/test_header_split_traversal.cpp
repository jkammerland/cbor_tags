#include <cbor_tags/cbor_traversal.h>
#include <cstddef>
#include <doctest/doctest.h>
#include <ranges>
#include <type_traits>
#include <vector>

TEST_CASE("traversal split header is directly usable") {
    using namespace cbor::tags;
    const std::vector<std::byte> input;
    auto                         dec     = make_decoder(input);
    const auto                   visitor = [](const auto &, const auto &) {};
    static_assert(noexcept(walk_item(dec, visitor)));
    static_assert(noexcept(validate_item(dec)));
    static_assert(std::same_as<decltype(walk_item(dec, visitor)), expected<void, status_code>>);
    static_assert(std::same_as<decltype(validate_item(dec)), expected<void, status_code>>);
    static_assert(std::same_as<decltype(walk_context<decltype(input.cbegin())>{}.source), std::ranges::subrange<decltype(input.cbegin())>>);
    CHECK_EQ(walk_options{}.max_depth, 64);
    CHECK_FALSE(walk_options{}.strict_validation);
    CHECK_EQ(validation_options{}.max_depth, 64);
    const auto result = walk_item(dec, visitor);
    REQUIRE_FALSE(result);
    CHECK(result.error() == status_code::incomplete);
}
