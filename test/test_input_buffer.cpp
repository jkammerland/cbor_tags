
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