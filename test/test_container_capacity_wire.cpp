#include "test_util.h"

#include <algorithm>
#include <cbor_tags/cbor_decoder.h>
#include <cstddef>
#include <doctest/doctest.h>
#include <ranges>
#include <vector>
#include <version>

#if __has_include(<boost/circular_buffer.hpp>) && __has_include(<boost/container/static_vector.hpp>)
#include <boost/circular_buffer.hpp>
#include <boost/container/static_vector.hpp>
#define CBOR_TAGS_TEST_BOOST_CAPACITY 1
#endif

#if defined(__cpp_lib_inplace_vector) && __cpp_lib_inplace_vector >= 202406L
#include <inplace_vector>
#endif

using namespace cbor::tags;

namespace {

template <typename MakeContainer> void check_capacity_bounds(MakeContainer make_container) {
    struct scenario {
        const char      *wire;
        std::vector<int> initial;
        std::vector<int> expected;
        status_code      status;
    };
    const scenario cases[]{
        {"8301020307", {9}, {9, 1, 2, 3}, status_code::success},
        {"9f010203ff07", {9}, {9, 1, 2, 3}, status_code::success},
        {"8401020304", {9}, {9}, status_code::size_limit_exceeded},
        {"9f01020304ff", {9}, {9, 1, 2, 3}, status_code::size_limit_exceeded},
        {"8101", {9, 8, 7, 6}, {9, 8, 7, 6}, status_code::size_limit_exceeded},
        {"9f01ff", {9, 8, 7, 6}, {9, 8, 7, 6}, status_code::size_limit_exceeded},
        {"8007", {9, 8, 7, 6}, {9, 8, 7, 6}, status_code::success},
        {"9fff07", {9, 8, 7, 6}, {9, 8, 7, 6}, status_code::success},
        {"811901", {9}, {9}, status_code::incomplete},
        {"9f1901", {9}, {9}, status_code::incomplete},
    };
    for (const auto &test : cases) {
        CAPTURE(test.wire);
        auto output = make_container();
        REQUIRE(static_cast<std::size_t>(output.capacity()) == 4U);
        for (int value : test.initial) {
            output.push_back(value);
        }
        const auto remaining = static_cast<std::size_t>(output.capacity()) - output.size();
        auto       bounded   = as_bounded_size(output, 0, remaining);
        const auto input     = to_bytes(test.wire);
        auto       dec       = make_decoder(input);
        const auto result    = dec(bounded);
        if (test.status == status_code::success) {
            REQUIRE(result);
            int following{};
            REQUIRE(dec(following));
            CHECK(following == 7);
        } else {
            REQUIRE_FALSE(result);
            CHECK(result.error() == test.status);
        }
        CHECK(std::ranges::equal(output, test.expected));
    }
}

template <typename Container> void check_zero_capacity() {
    const std::pair<const char *, bool> cases[]{{"8007", true}, {"9fff07", true}, {"8101", false}, {"9f01ff", false}};
    for (const auto &[wire, succeeds] : cases) {
        CAPTURE(wire);
        Container output;
        REQUIRE(static_cast<std::size_t>(output.capacity()) == 0U);
        const auto input  = to_bytes(wire);
        auto       dec    = make_decoder(input);
        const auto result = dec(as_bounded_size(output, 0, 0));
        if (succeeds) {
            REQUIRE(result);
        } else {
            REQUIRE_FALSE(result);
            CHECK(result.error() == status_code::size_limit_exceeded);
        }
        if (result) {
            int following{};
            REQUIRE(dec(following));
            CHECK(following == 7);
        }
        CHECK(output.empty());
    }
}

} // namespace

TEST_SUITE("cbor_wire/container_capacity") {

#ifdef CBOR_TAGS_TEST_BOOST_CAPACITY
    TEST_CASE("capacity bounds preserve circular buffer contents") {
        check_capacity_bounds([] { return boost::circular_buffer<int>(4); });
        check_zero_capacity<boost::circular_buffer<int>>();
    }

    TEST_CASE("capacity bounds preserve space optimized circular buffer contents") {
        check_capacity_bounds([] { return boost::circular_buffer_space_optimized<int>(4); });
        check_zero_capacity<boost::circular_buffer_space_optimized<int>>();
    }

    TEST_CASE("capacity bounds reject static vector overflow with a protocol status") {
        check_capacity_bounds([] { return boost::container::static_vector<int, 4>{}; });
        check_zero_capacity<boost::container::static_vector<int, 0>>();
    }
#endif

#if defined(__cpp_lib_inplace_vector) && __cpp_lib_inplace_vector >= 202406L
    TEST_CASE("capacity bounds reject inplace vector overflow with a protocol status") {
        check_capacity_bounds([] { return std::inplace_vector<int, 4>{}; });
        check_zero_capacity<std::inplace_vector<int, 0>>();
    }
#endif

} // TEST_SUITE("cbor_wire/container_capacity")
