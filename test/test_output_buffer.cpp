
#include "cbor_tags/cbor_encoder.h"
#include "test_util.h"

#include <deque>
#include <doctest/doctest.h>
#include <memory_resource>

using namespace cbor::tags;

TEST_CASE_TEMPLATE("CBOR Encoder", T, std::vector<std::byte>, std::deque<std::byte>) {
    auto [data, out] = make_data_and_encoder<T>();

    out.encode_value(1ull);
    out.encode_value(2ull);
    out.encode_value(3ull);

    CHECK_EQ(to_hex(data), "010203");
    CHECK_EQ(data.size(), 3);
}

TEST_CASE_TEMPLATE("CBOR with std::pmr", T, std::pmr::vector<std::byte>, std::pmr::deque<std::byte>) {
    std::array<std::byte, 1024>         buffer;
    std::pmr::monotonic_buffer_resource resource(buffer.data(), buffer.size());
    T                                   resource_vector(&resource);

    auto out = make_encoder<T>(resource_vector);

    out.encode_value(1ull);
    out.encode_value(2ull);
    out.encode_value(3ull);

    CHECK_EQ(to_hex(resource_vector), "010203");
}