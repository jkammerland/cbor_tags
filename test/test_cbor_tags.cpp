#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_reflection.h"
#include "cbor_tags/float16_ieee754.h"
#include "test_util.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <exception>
#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <format>
#include <list>
#include <map>
#include <nameof.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

using namespace cbor::tags;

std::string some_data = "Hello world!";
struct A {
    std::int64_t a;
    std::string  s;
};

// Helper function to print type and value
template <typename T> constexpr void print_type_and_value(const T &value) {
    if constexpr (std::is_same_v<T, A>) {
        fmt::print("Got type <A>: with values a={}, s={}\n", value.a, value.s);
    } else if constexpr (fmt::is_formattable<T>()) {
        fmt::print("Got type <{}> with value <{}>\n", nameof::nameof_short_type<T>(), value);
    } else {
        fmt::print("Got type <{}>\n", nameof::nameof_short_type<T>());
    }
}

TEST_CASE("Basic reflection") {
    struct M {
        int                a;
        std::optional<int> b;
    };

    auto count = detail::aggregate_binding_count<M>;
    fmt::print("M has {} members\n", count);
    CHECK_EQ(count, 2);

    auto is_constructible = IsBracesContructible<M, any, any>;
    fmt::print("M is braces construct with 2 members: {}\n", is_constructible);
    CHECK(is_constructible);

    // Check if we can construct M with any and any
    [[maybe_unused]] auto tmp1 = M{any{}, std::optional<int>{any{}}}; // This should compile

    [[maybe_unused]] auto tmp2 = M{any{}, any{}}; // Question is if if we can compile this
}

TEST_CASE("Advanced reflection") {
    struct Z {
        int                        a;
        float                      b;
        std::string                c;
        std::vector<int>           d;
        std::map<std::string, int> e;
        std::deque<double>         f;

        A g;

        std::optional<int>            h;
        std::optional<std::list<int>> i;
        std::vector<std::vector<int>> j;
        std::multimap<int, int>       k;
        std::set<std::pair<int, int>> l;
    };

    auto z = Z{42,
               3.14f,
               "Hello world!",
               {1, 2, 3},
               {{"one", 1}, {"two", 2}, {"three", 3}},
               {1.0, 2.0, 3.0},
               A{42, "Hello world!"},
               std::nullopt,
               std::list<int>{1, 2, 3},           // std::optional<std::list<int>>
               {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}}, // std::vector<std::vector<int>>
               {{1, 2}, {1, 2}, {1, 2}},          // std::multimap<int, int>
               {{1, 2}, {3, 4}, {5, 6}}};         // std::set<std::pair<int, int>>

    auto &&tuple = to_tuple(z);

    std::apply([](auto &&...args) { (print_type_and_value(args), ...); }, tuple);
    CHECK_EQ(detail::aggregate_binding_count<Z>, 12);
}

TEST_CASE("Basic tag") {
    auto tag_A = make_tag(140, A{-42, "Hello world!"});

    auto &&tuple = to_tuple(tag_A);

    std::apply([](auto &&...args) { (print_type_and_value(args), ...); }, tuple);

    // auto [data, out] = make_data_and_encoder<std::vector<std::byte>>();

    // out.encode(binary_tag_view{160, std::span<const std::byte>(reinterpret_cast<const std::byte *>(some_data.data()),
    // some_data.size())});

    // auto dataIn = data;
    // auto in     = make_decoder(dataIn);

    // binary_tag_view t;
    // auto            result = in.decode_value();
    // CHECK(std::holds_alternative<binary_tag_view>(result));
    // t = std::get<binary_tag_view>(result);

    // CHECK_EQ(t.tag, 160);
    // fmt::print("data encoded: {}\n", to_hex(data));
    // fmt::print("data decoded: {}\n", to_hex(t.data));
    // REQUIRE_EQ(t.data.size(), some_data.size());

    // CHECK(std::equal(some_data.begin(), some_data.end(), reinterpret_cast<const char *>(t.data.data())));
}