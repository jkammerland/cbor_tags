#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_reflection.h"
#include "cbor_tags/cbor_reflection_impl.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <fmt/format.h>
#include <iterator>
#include <limits>
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
    T          buffer;
    CborStream stream(buffer);

    fmt::print("name: {}\n", nameof::nameof_full_type<decltype(stream.buffer)>());
    if constexpr (std::is_const_v<T>) {
        CHECK(std::is_reference_v<decltype(stream.buffer)>);
        CHECK(std::is_const_v<std::remove_reference_t<decltype(stream.buffer)>>);
    } else {
        CHECK(std::is_reference_v<decltype(stream.buffer)>);
        CHECK(!std::is_const_v<std::remove_reference_t<decltype(stream.buffer)>>);
    }
}

TEST_CASE("Count struct members") {
    struct A {
        int    a;
        double b;
        char   c;
    };

    auto t = std::tuple<int, double, char, A>{};

    CHECK_EQ(detail::aggregate_binding_count<A>, 3);
    CHECK_EQ(detail::aggregate_binding_count<decltype(t)>, 4);

    struct B {};
    CHECK_EQ(detail::aggregate_binding_count<B>, 0);

    struct Eleven {
        int a;
        int b;
        int c;
        int d;
        int e;
        int f;
        struct C {
            int g;
            int h;
        };
        C   c1;
        int h;
        int i;
        int j;
        int k;
    };
    CHECK_EQ(detail::aggregate_binding_count<Eleven>, 11);
}

TEST_CASE("Reflection into tuple") {
    struct A {
        int    a;
        double b;
        char   c;
    };

    A a{1, 3.14, 'a'};

    auto &&t = to_tuple(a);

    CHECK_EQ(std::get<0>(t), 1);
    CHECK_EQ(std::get<1>(t), 3.14);
    CHECK_EQ(std::get<2>(t), 'a');

    CHECK_LT(detail::MAX_REFLECTION_MEMBERS, std::numeric_limits<size_t>::max());

    struct Eleven {
        int           a;
        int           b;
        char          c;
        std::uint16_t d;
        int           e;
        int           f;
        struct C {
            int g;
            int h;
        };
        C            c1;
        int          h;
        std::uint8_t i;
        int          j;
        std::string  k;
    };

    Eleven e{1, 2, 3, 4, 5, 6, {7, 8}, 9, 10, 11, "12"};

    auto &&t2 = to_tuple(e);
    CHECK_EQ(std::get<0>(t2), 1);
    CHECK_EQ(std::get<1>(t2), 2);
    CHECK_EQ(std::get<2>(t2), 3);
    CHECK_EQ(std::get<3>(t2), 4);
    CHECK_EQ(std::get<4>(t2), 5);
    CHECK_EQ(std::get<5>(t2), 6);
    auto &&[c, d] = std::get<6>(t2);
    CHECK_EQ(c, 7);
    CHECK_EQ(d, 8);
    CHECK_EQ(std::get<7>(t2), 9);
    CHECK_EQ(std::get<8>(t2), 10);
    CHECK_EQ(std::get<9>(t2), 11);
    CHECK_EQ(std::get<10>(t2), "12");
}