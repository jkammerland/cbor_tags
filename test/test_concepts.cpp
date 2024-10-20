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
#include <map>
#include <nameof.hpp>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
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

void test_ref(int &a) { a = 42; }

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

    c = 44;
    CHECK_EQ(std::get<6>(t2).g, 44);
    d = 55;
    CHECK_EQ(std::get<6>(t2).h, 55);

    auto &ref = std::get<9>(t2);
    test_ref(ref);
    CHECK_EQ(std::get<9>(t2), 42);
}

TEST_CASE("to_tupple address") {

    {
        struct A {
            int    a;
            double b;
            char   c;
        };

        A a{1, 3.14, 'a'};

        auto t = to_tuple(a);

        CHECK_EQ(&std::get<0>(t), &a.a);
        CHECK_EQ(&std::get<1>(t), &a.b);
        CHECK_EQ(&std::get<2>(t), &a.c);
    }

    {
        auto t           = std::make_tuple(1, 3.14, 'a');
        auto &&[a, b, c] = t;

        CHECK_EQ(&a, &std::get<0>(t));
        CHECK_EQ(&b, &std::get<1>(t));
        CHECK_EQ(&c, &std::get<2>(t));
    }

    {
        auto t          = std::make_tuple(1, 3.14, 'a');
        auto &[a, b, c] = t;

        CHECK_EQ(&a, &std::get<0>(t));
        CHECK_EQ(&b, &std::get<1>(t));
        CHECK_EQ(&c, &std::get<2>(t));
    }
}

TEST_CASE("Test more concepts for IsX") {
    {
        using map_1 = std::map<int, int>;
        static_assert(IsMap<map_1>);
        static_assert(!IsArray<map_1>);
        static_assert(!IsTuple<map_1>);
    }

    {
        using map_2 = std::unordered_map<int, int>;
        static_assert(IsMap<map_2>);
        static_assert(!IsArray<map_2>);
        static_assert(!IsTuple<map_2>);
    }

    {
        using opt_1 = std::optional<int>;
        static_assert(IsOptional<opt_1>);
        static_assert(!IsArray<opt_1>);
        static_assert(!IsMap<opt_1>);
        static_assert(!IsTuple<opt_1>);
        static_assert(!IsTextString<opt_1>);
        static_assert(!IsBinaryString<opt_1>);
    }

    {
        using array_1 = std::array<int, 5>;
        static_assert(IsArray<array_1>);
        static_assert(IsRange<array_1>);
        static_assert(!IsMap<array_1>);
        static_assert(!IsTuple<array_1>);
        static_assert(!IsOptional<array_1>);
    }

    {
        using tuple_1 = std::tuple<int, std::optional<int>>;
        static_assert(IsTuple<tuple_1>);
        static_assert(!IsRange<tuple_1>);
        static_assert(!IsArray<tuple_1>);
        static_assert(!IsMap<tuple_1>);
        static_assert(!IsOptional<tuple_1>);
    }

    {
        using string_1 = std::string;
        static_assert(IsTextString<string_1>);
        static_assert(IsRange<string_1>);
        static_assert(!IsBinaryString<string_1>);
        static_assert(!IsArray<string_1>);
        static_assert(!IsMap<string_1>);
        static_assert(!IsOptional<string_1>);
        static_assert(!IsTuple<string_1>);
    }

    {
        using string_1 = std::string_view;
        static_assert(IsTextString<string_1>);

        using string_2 = std::basic_string_view<char>;
        static_assert(IsTextString<string_2>);

        using string_3 = std::basic_string_view<std::byte>;
        static_assert(!IsTextString<string_3>);

        using string_4 = std::vector<char>;
        static_assert(!IsTextString<string_4>);
    }

    {
        using bstring_1 = std::basic_string<std::byte>;
        using bstring_2 = std::vector<std::byte>;
        using bstring_3 = std::basic_string_view<std::byte>;
        static_assert(IsBinaryString<bstring_1>);
        static_assert(!IsBinaryString<bstring_2>);
        static_assert(IsBinaryString<bstring_3>);
        static_assert(IsRange<bstring_1>);
        static_assert(!IsTextString<bstring_1>);
        static_assert(!IsTextString<bstring_2>);
        static_assert(!IsTextString<bstring_3>);
        static_assert(!IsArray<bstring_1>);
    }

    {
        using container = std::vector<int>;
        static_assert(IsRange<container>);
        static_assert(IsContiguous<container>);
    }

    {
        using container = std::list<int>;
        static_assert(IsRange<container>);
        static_assert(!IsContiguous<container>);
    }

    {
        using container = std::deque<int>;
        static_assert(IsRange<container>);
        static_assert(!IsContiguous<container>);
    }

    {
        using container = std::array<int, 5>;
        static_assert(IsRange<container>);
        static_assert(IsContiguous<container>);
    }
}

TEST_CASE("Test non aggregates") {
    {
        using var = std::variant<int, double>;
        using opt = std::optional<var>;
        static_assert(IsOptional<opt>);
        static_assert(!IsAggregate<opt>);
        static_assert(!IsVariant<opt>);
    }
    {
        static_assert(!IsAggregate<std::vector<int>>);
        static_assert(!IsAggregate<std::map<int, int>>);
        static_assert(!IsAggregate<std::tuple<int, int>>);
    }

    {
        using opt = std::optional<std::string>;
        using var = std::variant<int, opt>;
        static_assert(IsVariant<var>);
        static_assert(!IsOptional<var>);
        static_assert(!IsAggregate<var>);
        static_assert(!IsAggregate<std::vector<var>>);
        static_assert(!IsTuple<var>);
    }
}