
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
#include <string_view>
#include <variant>
#include <vector>

using namespace cbor::tags;

// TEST_CASE_TEMPLATE("Roundtrip", T, std::vector<std::byte>, std::deque<std::byte>) {
//     auto [data_in, in]   = make_data_and_decoder<T>();
//     auto [data_out, out] = make_data_and_encoder<T>();

//     out.encode_value(1ull);
//     out.encode_value(2ull);
//     out.encode_value(3ull);

//     // Emulate data transfer
//     data_in = data_out;

//     for (const auto &value : data_in) {
//         auto result = in.decode_value();
//         CHECK_EQ(std::holds_alternative<uint64_t>(result), true);
//         CHECK_EQ(std::get<uint64_t>(result), static_cast<uint64_t>(value));
//     }
// }

// TEST_CASE_TEMPLATE("Roundtrip binary cbor string", T, std::vector<char>) {
//     auto [data_in, in]   = make_data_and_decoder<T>();
//     auto [data_out, out] = make_data_and_encoder<T>();

//     using namespace std::string_view_literals;
//     auto sv =
//         "Hello, world, Hello, worldHello, worldHello, worldHello, worldHello, worldHello, worldHello, worldHello, worldHello, worldHello,
//         worldHello, worldHello, worldHello, worldHello, worldHello, worldHello, worldHello, worldHello, worldHello, worldHello,
//         worldHello, worldHello, worldHello, worldHello, worldHello, worldHello, worldHello, worldHello, worldHello, worldHello,
//         worldHello, worldHello, worldHello, worldHello, worldHello, worldHello, worldHello, worldHello, worldHello, worldHello,
//         worldHello, world"sv;
//     out.encode_value(sv);

//     // Emulate data transfer
//     data_in = data_out;

//     auto result = in.decode_value();
//     CHECK_EQ(std::holds_alternative<std::string_view>(result), true);
//     CHECK_EQ(std::get<std::string_view>(result), sv);
// }

// TODO: deque test