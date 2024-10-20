
#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "test_util.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <doctest/parts/doctest_fwd.h>
#include <fmt/core.h>
#include <forward_list>
#include <list>
#include <memory_resource>
#include <nameof.hpp>
#include <numeric>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

using namespace cbor::tags;

TEST_CASE_TEMPLATE("Roundtrip", T, std::vector<std::byte>, std::deque<std::byte>) {
    T data_in;
    auto [data_out, out] = make_data_and_encoder<T>();

    std::vector<uint64_t> values(1e5);
    std::iota(values.begin(), values.end(), 0);
    for (const auto &value : values) {
        out.encode(value);
    }

    // Emulate data transfer
    data_in = data_out;
    auto in = make_decoder(data_in);

    for (const auto &value : values) {
        auto result = in.decode_value();
        CHECK_EQ(std::holds_alternative<uint64_t>(result), true);
        CHECK_EQ(std::get<uint64_t>(result), static_cast<uint64_t>(value));
    }
}

TEST_CASE_TEMPLATE("Roundtrip binary cbor string", T, std::vector<char>, std::deque<char>, std::list<uint8_t>) {
    auto [data_out, out] = make_data_and_encoder<T>();

    using namespace std::string_view_literals;
    auto sv =
        "Hello world!Hello world!Hello world!Hello world!Hello world!Hello world!Hello world!Hello world!Hello world!Hello world!Hello world!Hello world!Hello world!Hello world!Hello world!Hello world!Hello world!Hello world!Hello world!Hello world!Hello world!Hello world!"sv;
    out.encode(sv);

    // Emulate data transfer
    auto data_in = data_out;
    auto in      = make_decoder(data_in);

    auto result = in.decode_value();
    if constexpr (IsContiguous<T>) {
        CHECK_EQ(std::holds_alternative<std::string_view>(result), true);
        CHECK_EQ(std::get<std::string_view>(result), sv);
    } else {
        using iterator_t = decltype(in)::iterator_t;
        using char_range = char_range_view<std::ranges::subrange<iterator_t>>;
        REQUIRE_EQ(std::holds_alternative<char_range>(result), true);
        auto range = std::get<char_range>(result).range;
        CHECK_EQ(std::equal(sv.begin(), sv.end(), range.begin()), true);
    }
}

TEST_CASE_TEMPLATE("Roundtrip binary cbor tagged array", T, std::vector<char>, std::deque<std::byte>, std::list<uint8_t>) {
    auto [data_out, out] = make_data_and_encoder<T>();

    std::vector<variant_contiguous> values(1e1);
    std::iota(values.begin(), values.end(), 0);

    [[maybe_unused]] auto t = make_tag_pair(tag<123>{}, values);
    // out.encode(tag);
}

TEST_CASE_TEMPLATE("Decode array of ints", T, std::vector<unsigned char>) {
    T data;
    data.reserve(1000);
    auto out = make_encoder(data);

    std::vector<uint64_t> values(10);
    std::iota(values.begin(), values.end(), 0);
    out(values);

    auto                  in = make_decoder(data);
    std::vector<uint64_t> result;
    in(result);

    REQUIRE_EQ(values.size(), result.size());
    CHECK_EQ(values, result);
}

TEST_CASE_TEMPLATE("Decode array of strings", T, std::vector<char>, std::deque<char>, std::list<char>) {
    T data;
    if constexpr (HasReserve<T>) {
        data.reserve(1000);
    }
    auto out = make_encoder(data);

    std::vector<std::string> values(10);
    std::generate(values.begin(), values.end(), [i = 0]() mutable { return fmt::format("Hello world {}", i++); });
    out(values);

    auto                     in = make_decoder(data);
    std::vector<std::string> result;
    in(result);

    REQUIRE_EQ(values.size(), result.size());
    CHECK_EQ(std::equal(values.begin(), values.end(), result.begin()), true);
}

TEST_CASE_TEMPLATE("Decode tagged types", T, std::vector<std::byte>, std::deque<std::byte>, std::list<std::byte>) {
    T data;
    if constexpr (HasReserve<T>) {
        data.reserve(1000);
    }
    auto out = make_encoder(data);

    struct A {
        int                        a;
        double                     b;
        std::optional<std::string> c;
        std::vector<int>           d;
    };

    using namespace cbor::tags::literals;
    out(make_tag_pair(123_tag, A{1, 3.14, "Hello", {1, 2, 3}}));

    auto hex = to_hex(data);
    fmt::print("to_hex: {}\n", hex);
    REQUIRE_EQ(hex, "d87b01fb40091eb851eb851f6548656c6c6f83010203");

    auto in     = make_decoder(data);
    auto result = make_tag_pair(123_tag, A{});
    in(result);

    CHECK_EQ(result.second.a, 1);
    CHECK_EQ(result.second.b, 3.14);
    CHECK_EQ(result.second.c, "Hello");
    CHECK_EQ(result.second.d, std::vector<int>({1, 2, 3}));
}