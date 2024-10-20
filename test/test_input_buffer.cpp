
#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_decoder.h"
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
#include <utility>
#include <variant>

using namespace cbor::tags;

TEST_CASE_TEMPLATE("CBOR Decoder", T, std::vector<char>, std::deque<std::byte>) {

    using value_type = typename T::value_type;

    std::vector<value_type> encoded;
    encoded.push_back(value_type{0x01});
    encoded.push_back(value_type{0x02});
    encoded.push_back(value_type{0x03});

    T    data(encoded.begin(), encoded.end());
    auto in = make_decoder<T>(data);

    for (const auto &value : encoded) {
        auto result = in.decode_value();
        CHECK_EQ(std::holds_alternative<uint64_t>(result), true);
        CHECK_EQ(std::get<uint64_t>(result), static_cast<uint64_t>(value));
    }
}

TEST_CASE_TEMPLATE("CBOR decode from array", T, std::array<unsigned char, 5>, std::deque<char>) {
    T data;
    if constexpr (std::is_same_v<T, std::array<unsigned char, 5>>) {
        data = {0x01, 0x02, 0x03, 0x04, 0x05};
    } else {
        data = {'\x01', '\x02', '\x03', '\x04', '\x05'};
    }

    auto in = make_decoder(data);

    for (const auto &value : data) {
        auto result = in.decode_value();
        CHECK_EQ(std::holds_alternative<uint64_t>(result), true);
        CHECK_EQ(std::get<uint64_t>(result), static_cast<uint64_t>(value));
    }
}

template <typename T> void set1(T &a) {
    auto &[b] = a;
    b         = 6.28;
}

TEST_CASE_TEMPLATE("Test input tag 123", T, std::vector<uint8_t>) {
    using namespace std::string_view_literals;
    auto bytes = to_bytes("d87bfb40091eb851eb851f"sv);

    auto in = make_decoder(bytes);
    struct A {
        double b;
    };
    auto a             = make_tag_pair(tag<123>{}, A{});
    auto &[tag, value] = a;
    value.b            = 3.14;
    CHECK_EQ(tag, 123);
    CHECK_EQ(value.b, 3.14);

    set1(std::get<1>(a));
    CHECK_EQ(value.b, 6.28);

    A    c{3.4};
    auto tuple         = to_tuple(c);
    std::get<0>(tuple) = 3.0;
    CHECK_EQ(c.b, 3.0);

    in(a);
    CHECK_EQ(value.b, 3.14);
}