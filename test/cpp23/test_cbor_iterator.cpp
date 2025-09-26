#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_element_iterator.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_integer.h"
#include "cbor_tags/cbor_operators.h"
#include "cbor_tags/float16_ieee754.h"
#include "test_util.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <exception>
#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

using namespace cbor::tags;
using namespace std::string_view_literals;

TEST_CASE("iterator") {
    auto data   = std::vector<std::byte>();
    auto enc    = make_encoder(data);
    std::ignore = enc(1, 2, 3, 4, "Hello"sv);

    fmt::println("{}", to_hex(data));

    // Iterate through CBOR elements (returns positions)
    auto view = make_cbor_view(data);
    for (auto pos : view) {
        // Get a decoder at this position if you want to decode
        auto dec = view.decoder_at(pos);
        // Decode whatever you need...
        int result;
        if (dec(result)) {
            fmt::println("{}", result);
        }
    }

    // Or use with ranges algorithms
    // std::ranges::to<std::vector<uint8_t>>();
    auto positions = view | std::views::take(3) | std::ranges::to<std::vector>();
    // fmt::println("First 3 positions: {}", positions);
}