#include <algorithm>
#include <array>
#include <cbor_tags/cbor_detail.h>
#include <cstddef>
#include <deque>
#include <doctest/doctest.h>
#include <list>
#include <span>
#include <string_view>
#include <vector>

TEST_SUITE("detail/appender") {
    TEST_CASE_TEMPLATE("dynamic byte append accepts empty views and preserves existing bytes", Buffer, std::vector<std::byte>,
                       std::deque<std::byte>, std::list<std::byte>) {
        Buffer buffer;
        SUBCASE("empty output") {}
        SUBCASE("existing output") { buffer = {std::byte{1}, std::byte{2}}; }

        const auto                           original = buffer;
        cbor::tags::detail::appender<Buffer> append;
        append(buffer, std::span<const std::byte>{});
        append(buffer, std::string_view{});
        CHECK(buffer == original);

        const std::array payload{std::byte{3}, std::byte{4}};
        append(buffer, std::span<const std::byte>{payload.data(), 0});
        append(buffer, std::string_view{"abc", 0});
        CHECK(buffer == original);

        append(buffer, std::span<const std::byte>{payload});
        append(buffer, std::string_view{"xy"});
        auto expected = original;
        expected.push_back(std::byte{3});
        expected.push_back(std::byte{4});
        expected.push_back(std::byte{'x'});
        expected.push_back(std::byte{'y'});
        CHECK(buffer == expected);

        append(buffer, std::span<const std::byte>{});
        append(buffer, std::string_view{});
        CHECK(buffer == expected);
    }
}
