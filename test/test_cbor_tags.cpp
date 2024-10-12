#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/float16_ieee754.h"
#include "test_util.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <exception>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <format>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace cbor::tags;

std::string some_data = "Hello world!";

TEST_CASE("Basic tag") {
    auto [data, out] = make_data_and_encoder<std::vector<std::byte>>();

    out.encode(binary_tag_view{160, std::span<const std::byte>(reinterpret_cast<const std::byte *>(some_data.data()), some_data.size())});

    auto dataIn = data;
    auto in     = make_decoder(dataIn);

    binary_tag_view t;
    auto            result = in.decode_value();
    CHECK(std::holds_alternative<binary_tag_view>(result));
    t = std::get<binary_tag_view>(result);

    CHECK_EQ(t.tag, 160);
    fmt::print("data encoded: {}\n", to_hex(data));
    fmt::print("data decoded: {}\n", to_hex(t.data));
    REQUIRE_EQ(t.data.size(), some_data.size());

    CHECK(std::equal(some_data.begin(), some_data.end(), reinterpret_cast<const char *>(t.data.data())));
}