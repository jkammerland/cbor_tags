
#include "cbor_tags/cbor_encoder.h"
#include "test_util.h"

#include <array>
#include <deque>
#include <doctest/doctest.h>
#include <doctest/parts/doctest_fwd.h>
#include <fmt/core.h>
#include <list>
#include <memory_resource>
#include <nameof.hpp>

using namespace cbor::tags;

TEST_CASE_TEMPLATE("CBOR Encoder", T, std::vector<std::byte>, std::deque<std::byte>, std::array<std::byte, 1024>) {
    auto [data, out] = make_data_and_encoder<T>();

    out.encode_value(1ull);
    out.encode_value(2ull);
    out.encode_value(3ull);

    if constexpr (!detail::IsArrayConcept<T>) {
        CHECK_EQ(to_hex(data), "010203");
        CHECK_EQ(data.size(), 3);
    } else {
        CHECK_EQ(to_hex(std::span(data.data(), 3)), "010203");
    }
}

TEST_CASE_TEMPLATE("CBOR with std::pmr", T, std::pmr::vector<std::byte>, std::pmr::deque<char>, std::pmr::deque<uint8_t>,
                   std::pmr::list<char>, std::pmr::list<uint8_t>) {
    fmt::print("Testing with T: {}\n", nameof::nameof_type<T>());

    std::array<std::byte, 1024>         buffer;
    std::pmr::monotonic_buffer_resource resource(buffer.data(), buffer.size());
    T                                   resource_vector(&resource);

    auto out = make_encoder<T>(resource_vector);

    out.encode_value(1ull);
    out.encode_value(2ull);
    out.encode_value(3ull);

    CHECK_EQ(to_hex(resource_vector), "010203");
}