#include "cbor_tags/cbor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <fmt/format.h>
#include <iterator>
#include <list>
#include <nameof.hpp>
#include <type_traits>
#include <vector>

using namespace cbor::tags;

TEST_CASE_TEMPLATE("CBOR buffer concept", T, std::byte, std::uint8_t, char, unsigned char) {
    fmt::print("Testing concept with T: {}\n", nameof::nameof_type<T>());
    CHECK(cbor::tags::ValidCborBuffer<std::array<T, 5>>);
    CHECK(cbor::tags::ValidCborBuffer<std::vector<T>>);
    CHECK(cbor::tags::ValidCborBuffer<std::list<T>>);
    CHECK(cbor::tags::ValidCborBuffer<std::deque<T>>);
}

TEST_CASE("Contiguous range concept") {
    CHECK(cbor::tags::IsContiguous<std::array<std::byte, 5>>);
    CHECK(cbor::tags::IsContiguous<std::vector<std::byte>>);
    CHECK(!cbor::tags::IsContiguous<std::list<std::byte>>);
    CHECK(!cbor::tags::IsContiguous<std::deque<std::byte>>);
}

struct A {
    template <typename T>
        requires IsContiguous<T>
    A(T) {
        fmt::print("A() contiguous\n");
    }

    template <typename T>
        requires(!IsContiguous<T>)
    A(T) {
        fmt::print("A() not contiguous\n");
    }
};

TEST_CASE("Concept sfinae") {
    A a(std::array<std::byte, 5>{});
    A b(std::vector<std::byte>{});
    A c(std::list<std::byte>{});
    A d(std::deque<std::byte>{});
}

TEST_CASE("Test iterator concepts") {
    CHECK(std::input_or_output_iterator<std::vector<uint8_t>::iterator>);
    CHECK(std::forward_iterator<std::vector<uint8_t>::iterator>);
    CHECK(std::random_access_iterator<std::deque<uint8_t>::iterator>);
    CHECK(std::bidirectional_iterator<std::list<uint8_t>::iterator>);
}

TEST_CASE_TEMPLATE("Cbor stream", T, std::vector<uint8_t>, const std::vector<uint8_t>) {
    T           buffer;
    cbor_stream stream(buffer);

    fmt::print("name: {}\n", nameof::nameof_full_type<decltype(stream.buffer)>());
    if constexpr (std::is_const_v<T>) {
        CHECK(std::is_reference_v<decltype(stream.buffer)>);
        CHECK(std::is_const_v<std::remove_reference_t<decltype(stream.buffer)>>);
    } else {
        CHECK(std::is_reference_v<decltype(stream.buffer)>);
        CHECK(!std::is_const_v<std::remove_reference_t<decltype(stream.buffer)>>);
    }
}
