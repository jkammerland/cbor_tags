
#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "test_util.h"

#include <array>
#include <cstddef>
#include <deque>
#include <doctest/doctest.h>
#include <doctest/parts/doctest_fwd.h>
#include <fmt/core.h>
#include <list>
#include <memory_resource>
#include <nameof.hpp>
#include <numeric>
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
        out.encode_value(value);
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
    out.encode_value(sv);

    // Emulate data transfer
    auto data_in = data_out;
    auto in      = make_decoder(data_in);

    auto result = in.decode_value();
    if constexpr (IsContiguous<T>) {
        CHECK_EQ(std::holds_alternative<std::string_view>(result), true);
        CHECK_EQ(std::get<std::string_view>(result), sv);
    } else {
        using iterator_t = typename iterator_type<T>::type;
        using char_range = char_range_view<std::ranges::subrange<iterator_t>>;
        REQUIRE_EQ(std::holds_alternative<char_range>(result), true);
        auto range = std::get<char_range>(result).range;
        CHECK_EQ(std::equal(sv.begin(), sv.end(), range.begin()), true);
    }
}

TEST_CASE_TEMPLATE("Roundtrip binary cbor tagged array", T, std::vector<char>, std::deque<std::byte>, std::list<uint8_t>) {
    auto [data_out, out] = make_data_and_encoder<T>();

    std::vector<value> values(1e1);
    std::iota(values.begin(), values.end(), 0);

    [[maybe_unused]] auto tag = make_tag(1, values);
    // out.encode_value(tag);
}