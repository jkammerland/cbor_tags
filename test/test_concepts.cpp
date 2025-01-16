#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_concepts_checking.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_integer.h"
#include "cbor_tags/cbor_reflection.h"
#include "cbor_tags/cbor_reflection_impl.h"
#include "cbor_tags/cbor_simple.h"
#include "cbor_tags/float16_ieee754.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <nameof.hpp>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>
using namespace cbor::tags;

TEST_CASE("Test IsUnsigned concept") {
    static_assert(IsUnsigned<std::uint8_t>);
    static_assert(IsUnsigned<std::uint16_t>);
    static_assert(!IsUnsigned<int>);
}

TEST_CASE("Test IsSigned concept") {
    static_assert(IsSigned<char>);
    static_assert(IsSigned<int>);
    static_assert(IsSigned<std::int8_t>);
    static_assert(IsSigned<std::int16_t>);
    static_assert(!IsSigned<std::uint8_t>);
}

TEST_CASE("Test IsTextString concept") {
    static_assert(IsTextString<std::string>);
    static_assert(IsTextString<std::string_view>);
    static_assert(IsTextString<std::basic_string_view<char>>);
    static_assert(!IsTextString<std::basic_string_view<std::byte>>);
    static_assert(!IsTextString<std::vector<char>>);
    static_assert(!IsTextString<std::span<char>>);
    static_assert(!IsTextString<std::span<const std::byte>>);
}

TEST_CASE("Test IsBinaryString concept") {
    static_assert(IsBinaryString<std::basic_string<std::byte>>);
    static_assert(IsBinaryString<std::basic_string_view<std::byte>>);
    static_assert(IsBinaryString<std::vector<std::byte>>);
    static_assert(IsBinaryString<std::array<std::byte, 5>>);
    static_assert(IsBinaryString<std::span<const std::byte>>);
    static_assert(!IsBinaryString<std::vector<uint8_t>>);
    static_assert(!IsBinaryString<std::span<const uint8_t>>);
    static_assert(!IsBinaryString<std::basic_string_view<uint8_t>>);
    static_assert(!IsBinaryString<std::string>);
    static_assert(!IsBinaryString<std::string_view>);
    static_assert(!IsRangeOfCborValues<std::array<std::byte, 5>>);
    static_assert(!IsRangeOfCborValues<std::vector<std::byte>>);
}

TEST_CASE("Test IsMap concept with std::map") {
    using map_1 = std::map<int, int>;
    static_assert(IsMap<map_1>);
    static_assert(IsRangeOfCborValues<map_1>);
    static_assert(!IsFixedArray<map_1>);
    static_assert(!IsTuple<map_1>);
    static_assert(!IsArray<map_1>);
    static_assert(!IsOptional<map_1>);
}

TEST_CASE("Test IsMap concept with std::unordered_map") {
    using map_2 = std::unordered_map<int, int>;
    static_assert(IsMap<map_2>);
    static_assert(!IsFixedArray<map_2>);
    static_assert(!IsTuple<map_2>);
}

TEST_CASE("Test IsOptional concept") {
    using opt_1 = std::optional<int>;
    static_assert(IsOptional<opt_1>);
    static_assert(!IsFixedArray<opt_1>);
    static_assert(!IsMap<opt_1>);
    static_assert(!IsTuple<opt_1>);
    static_assert(!IsTextString<opt_1>);
    static_assert(!IsBinaryString<opt_1>);
}

enum class E { A, B, C, D };

struct TEST0 {
    static constexpr std::size_t cbor_tag = 0x01;
    E                            e;
    double                       d;
};

TEST_CASE_TEMPLATE("Test IsOptional concept with std::optional", T, std::optional<int>, std::optional<std::string>,
                   std::optional<std::byte>, std::optional<E>, std::optional<std::variant<E, std::string>>, std::optional<TEST0>) {
    static_assert(IsOptional<T>);
}

TEST_CASE("Test IsRangeOfCborValues concept") {
    using array_1 = std::array<int, 5>;
    static_assert(IsFixedArray<array_1>);
    static_assert(IsRangeOfCborValues<array_1>);
    static_assert(!IsMap<array_1>);
    static_assert(!IsTuple<array_1>);
    static_assert(!IsOptional<array_1>);

    using array_2 = std::vector<std::byte>;
    static_assert(!IsRangeOfCborValues<array_2>);
}

TEST_CASE_TEMPLATE("Test IsArray", T, std::vector<int>, std::deque<int>, std::list<int>, std::array<int, 5>) {
    static_assert(IsArray<T>);
    static_assert(IsRangeOfCborValues<T>);
    static_assert(!IsMap<T>);
    static_assert(!IsAggregate<T>);
}

TEST_CASE("Test HasCborTag and IsTagged concepts") {
    struct CBOR1 {
        std::uint64_t cbor_tag = 1;
    };

    static_assert(HasInlineTag<CBOR1>);
    static_assert(IsTag<CBOR1>);
    static_assert(!IsTaggedTuple<CBOR1>);

    struct CBOR2 {
        std::uint64_t cbor_ = 2;
    };

    static_assert(!HasStaticTag<CBOR2>);
    static_assert(!IsTag<CBOR2>);
}

TEST_CASE("Test IsTuple concept") {
    using tuple_1 = std::tuple<int, std::optional<int>>;
    static_assert(IsTuple<tuple_1>);
    static_assert(!IsRangeOfCborValues<tuple_1>);
    static_assert(!IsFixedArray<tuple_1>);
    static_assert(!IsMap<tuple_1>);
    static_assert(!IsOptional<tuple_1>);
}

TEST_CASE("Test IsTagged concept with tagged tuples") {
    auto tagged = std::make_tuple(static_tag<1>{}, 1);
    static_assert(IsTag<decltype(tagged)>);

    auto tagged_tuple = std::make_tuple(static_tag<1>{}, std::make_tuple(1, 2));
    static_assert(IsTag<decltype(tagged_tuple)>);

    auto tagged_tuple_2 = std::make_pair(static_tag<1>{}, std::make_tuple(1, 2));
    static_assert(IsTag<decltype(tagged_tuple_2)>);
    static_assert(!IsVariant<decltype(tagged_tuple_2)>);
    static_assert(!IsOptional<decltype(tagged_tuple_2)>);
    static_assert(!IsFixedArray<decltype(tagged_tuple_2)>);
    static_assert(!IsMap<decltype(tagged_tuple_2)>);

    auto tagged_tuple_3 = std::tuple(static_tag<1>{}, std::make_tuple(1, 2), std::make_tuple(3, 4));
    static_assert(IsTag<decltype(tagged_tuple_3)>);

    auto tagged_tuple_4 = std::tuple(1, static_tag<1>{});
    static_assert(!IsTag<decltype(tagged_tuple_4)>);
}

struct DefCbor {
    static constexpr std::uint64_t cbor_tag = 1;
};

struct NotCbor {};

TEST_CASE_TEMPLATE("IsCborMajor Positive", T, std::uint8_t, int, double, std::string, std::vector<int>, std::map<int, int>,
                   tagged_object<static_tag<5>, int>, std::variant<int, double, DefCbor, tagged_object<static_tag<2>, NotCbor>>,
                   std::optional<int>, std::optional<std::string>, std::optional<std::byte>,
                   std::optional<std::variant<int, double, DefCbor, std::variant<std::string, std::vector<std::byte>>>>,
                   std::map<int, DefCbor>, std::unordered_map<DefCbor, int>, std::optional<TEST0>) {
    static_assert(IsCborMajor<T>);
}

TEST_CASE_TEMPLATE("IsCborMajor negative", T, NotCbor, std::variant<int, NotCbor>, std::optional<NotCbor>,
                   std::optional<std::variant<int, NotCbor>>, std::tuple<int, NotCbor>, std::pair<int, NotCbor>, std::vector<NotCbor>,
                   std::map<NotCbor, int>, std::unordered_map<int, NotCbor>, std::tuple<std::uint64_t, int>) {
    static_assert(!IsCborMajor<T>);
}

TEST_CASE("Test IsTextString concept with various string types") {
    using string_1 = std::string;
    static_assert(IsTextString<string_1>);
    static_assert(!IsRangeOfCborValues<string_1>);
    static_assert(!IsBinaryString<string_1>);
    static_assert(!IsFixedArray<string_1>);
    static_assert(!IsMap<string_1>);
    static_assert(!IsOptional<string_1>);
    static_assert(!IsTuple<string_1>);
}

TEST_CASE("Test IsTextString concept with string views") {
    using string_1 = std::string_view;
    static_assert(IsTextString<string_1>);

    using string_2 = std::basic_string_view<char>;
    static_assert(IsTextString<string_2>);

    using string_3 = std::basic_string_view<std::byte>;
    static_assert(!IsTextString<string_3>);

    using string_4 = std::vector<char>;
    static_assert(!IsTextString<string_4>);
}

TEST_CASE("Test IsBinaryString concept with various binary string types") {
    using bstring_1 = std::basic_string<std::byte>;
    using bstring_2 = std::vector<std::byte>;
    using bstring_3 = std::basic_string_view<std::byte>;
    using bstring_4 = std::array<std::byte, 5>;
    static_assert(IsBinaryString<bstring_1>);
    static_assert(IsBinaryString<bstring_2>);
    static_assert(IsBinaryString<bstring_3>);
    static_assert(IsBinaryString<bstring_4>);
    static_assert(!IsAggregate<bstring_4>);
    static_assert(!IsRangeOfCborValues<bstring_1>);
    static_assert(!IsTextString<bstring_1>);
    static_assert(!IsTextString<bstring_2>);
    static_assert(!IsTextString<bstring_3>);
    static_assert(!IsFixedArray<bstring_1>);
}

TEST_CASE("Test IsRangeOfCborValues and IsContiguous concepts with containers") {
    using container = std::vector<int>;
    static_assert(IsRangeOfCborValues<container>);
    static_assert(IsContiguous<container>);
}

TEST_CASE("Test IsRangeOfCborValues and IsContiguous concepts with non-contiguous containers") {
    using container = std::list<int>;
    static_assert(IsRangeOfCborValues<container>);
    static_assert(!IsContiguous<container>);
}

TEST_CASE("Test IsRangeOfCborValues and IsContiguous concepts with deque") {
    using container = std::deque<int>;
    static_assert(IsRangeOfCborValues<container>);
    static_assert(!IsContiguous<container>);
}

TEST_CASE("Test IsRangeOfCborValues and IsContiguous concepts with array") {
    using container = std::array<int, 5>;
    static_assert(IsRangeOfCborValues<container>);
    static_assert(IsContiguous<container>);
}

TEST_CASE_TEMPLATE("Test simple concepts positive", T, bool, std::nullptr_t, float, double, float16_t, simple) {
    static_assert(IsSimple<T>);
    static_assert(!IsAggregate<T>);
    static_assert(!IsOptional<T>);
    static_assert(!IsVariant<T>);
    static_assert(!IsFixedArray<T>);
    static_assert(!IsMap<T>);
    static_assert(!IsTuple<T>);
    static_assert(!IsTextString<T>);
    static_assert(!IsBinaryString<T>);
}

TEST_CASE_TEMPLATE("Test simple concepts negative", T, std::string, std::vector<int>, std::map<int, int>, std::tuple<int, int>, int,
                   uint8_t, std::uint64_t, char, std::nullopt_t, integer, negative) {
    static_assert(!IsSimple<T>);
}

TEST_CASE("Test non aggregates, to_tuple(Type) will not work for Type that does not meet IsAggregate<Type>") {
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
        static_assert(!IsAggregate<negative>);
        static_assert(!IsAggregate<integer>);
        static_assert(!IsAggregate<simple>);
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

TEST_CASE_TEMPLATE("Test IsVariant", T, std::variant<int, double>, std::variant<int, std::optional<int>>) {
    static_assert(IsVariant<T>);
    static_assert(!IsOptional<T>);
}

TEST_CASE_TEMPLATE("Test IsVariant negative", T, std::tuple<int, double>, std::map<int, double>, std::optional<int>) {
    static_assert(!IsVariant<T>);
}

TEST_CASE_TEMPLATE("Test IsStrictVariant", T, std::variant<int, double>, std::variant<int, bool>, std::variant<std::int64_t, bool>,
                   std::variant<positive, negative>, std::variant<std::string, negative, positive>, std::variant<std::string, bool>,
                   std::variant<int64_t, bool>, std::variant<negative, bool>, std::variant<positive, bool>, std::variant<double, float>) {
    static_assert(IsStrictVariant<T>);
}

TEST_CASE_TEMPLATE("Test IsStrictVariant negative", T, std::variant<int, negative>, std::variant<int, positive>,
                   std::variant<std::string, int, negative>, std::variant<std::string, int, positive>) {
    static_assert(!IsStrictVariant<T>);
}

TEST_CASE_TEMPLATE("Not IsAggregate<T>", T, std::vector<int>, std::map<int, int>, std::tuple<int, int>, std::pair<static_tag<1>, int>,
                   std::optional<int>, float, double, std::string, std::string_view, std::byte, std::uint8_t, std::int8_t, std::uint64_t,
                   std::int64_t, std::nullptr_t) {
    static_assert(!IsAggregate<T>);
}

TEST_CASE("IsAggregate<T>") {
    struct A {};
    struct B {
        A a;
    };
    struct C {
        A a;
        B b;
    };
    struct D {
        A a;
        B b;
        C c;
    };
    struct E {
        A                            a;
        B                            b;
        C                            c;
        D                            d;
        std::variant<int, double, A> v;
    };

    static_assert(IsAggregate<A>);
    static_assert(IsAggregate<B>);
    static_assert(IsAggregate<C>);
    static_assert(IsAggregate<D>);
    static_assert(IsAggregate<E>);

    // Check to_tuple, TODO: add rhs ref overload for to_tuple
    [[maybe_unused]] auto a = to_tuple(A{});
    [[maybe_unused]] auto b = to_tuple(B{});
    [[maybe_unused]] auto c = to_tuple(C{});
    [[maybe_unused]] auto d = to_tuple(D{});
    [[maybe_unused]] auto e = to_tuple(E{});
}

struct TAGMAJORTYPE {
    static constexpr std::uint64_t cbor_tag = 123;
    int                            a;
};

struct NONMAJORTYPE {
    int a;
};

template <typename Byte, typename... Types> auto collect_concept_types() {
    (fmt::print("Types: {}, value: <{}>\n", nameof::nameof_type<Types>(), static_cast<int>(ConceptType<Byte, Types>::value)), ...);
    return std::set<Byte>{ConceptType<Byte, Types>::value...};
}

TEST_CASE_TEMPLATE("Test which concept type major type", T, std::byte, char, uint8_t) {
    auto set = collect_concept_types<T, std::uint8_t, double, float, negative, std::int8_t, std::map<int, std::string>,
                                     std::array<uint8_t, 5>, std::string, std::vector<std::byte>, TAGMAJORTYPE, NONMAJORTYPE>();

    fmt::print("Set contains: [{}], size of set is <{}>\n", fmt::join(set, ", "), set.size());
    CHECK_EQ(set.size(), 9);
}

template <typename Byte, typename... T> bool wrapper(Byte b, const std::variant<T...> &) { return is_valid_major<Byte, T...>(b); }

TEST_CASE_TEMPLATE("Test variants in any concept for major type", T, std::byte, char, uint8_t) {
    using var1 = std::variant<double, int, std::vector<std::byte>>;
    using var2 = std::variant<std::uint64_t, negative, std::span<const std::byte>, std::string_view, std::optional<uint8_t>>;
    CHECK(wrapper(static_cast<T>(0), var1{})); // 0 is Unsigned, thus int will not match
    CHECK(wrapper(static_cast<T>(0), var2{}));

    CHECK(wrapper(static_cast<T>(1), var1{}));
    CHECK(wrapper(static_cast<T>(1), var2{}));

    CHECK(wrapper(static_cast<T>(2), var1{}));
    CHECK(wrapper(static_cast<T>(2), var2{}));

    // CHECK(!wrapper(static_cast<T>(3), var1{}));
}

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

struct AllCborMajorsExample {
    bool is_contiguous;

    template <typename T>
        requires IsContiguous<T>
    AllCborMajorsExample(T) : is_contiguous(true) {
        fmt::print("A() contiguous\n");
    }

    template <typename T>
        requires(!IsContiguous<T>)
    AllCborMajorsExample(T) : is_contiguous(false) {
        fmt::print("A() not contiguous\n");
    }
};

TEST_CASE("Concept sfinae") {
    AllCborMajorsExample a(std::array<std::byte, 5>{});
    AllCborMajorsExample b(std::vector<std::byte>{});
    AllCborMajorsExample c(std::list<std::byte>{});
    AllCborMajorsExample d(std::deque<std::byte>{});

    CHECK(a.is_contiguous);
    CHECK(a.is_contiguous);
    CHECK(!c.is_contiguous);
    CHECK(!d.is_contiguous);
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

// Example function that uses tags
template <std::uint64_t N> constexpr void process_tag(static_tag<N>) { fmt::print("Processing tag value: 0x{:x} ({})\n", N, N); }

TEST_CASE("Literals") {
    using namespace cbor::tags::literals;

    // Using hex tags
    process_tag(0xFF_hex_tag);   // Will print: Processing tag value: 0xff (255)
    process_tag(0xDEAD_hex_tag); // Will print: Processing tag value: 0xdead (57005)

    static_assert(decltype(255_tag)::cbor_tag == 255);
    static_assert(decltype(0xFF_hex_tag)::cbor_tag == 255);
    static_assert(decltype(0xABC_hex_tag)::cbor_tag == 2748);
}

enum class E1 { A, B, C, D };

struct A2 {
    static constexpr std::uint64_t cbor_tag = 1;
    E1                             e;
    double                         d;
};

struct B2 {
    static constexpr std::uint64_t cbor_tag = 2;
    E1                             e;
    double                         d;
};

TEST_CASE_TEMPLATE(
    "ValidConceptMapping test positive", T, std::variant<int, double>, std::variant<double, std::string, std::vector<std::byte>>,
    std::variant<int, float, std::string, std::vector<std::byte>, std::vector<uint16_t>, static_tag<2>, std::map<int, std::string>>,
    std::variant<uint64_t, negative>, std::variant<static_tag<1>, static_tag<2>, static_tag<3>>, std::variant<static_tag<1>, E1>,
    std::variant<static_tag<1>, E1, B2>,
    std::variant<float16_t, float, double, bool, std::nullptr_t, simple, int, std::string, std::span<const std::byte>, B2, A2,
                 std::array<int, 5>, std::map<int, std::string>>) {

    // Use lambdas to wrap concepts

    auto result    = valid_concept_mapping_v<T>;
    auto array     = valid_concept_mapping_array_v<T>;
    auto unmatched = valid_concept_mapping_n_unmatched_v<T>;
    fmt::print("Array: {}, Expecting <true>: Got: <{}>\n", array, result);

    CHECK(result);
    CHECK_EQ(unmatched, 0);
}

TEST_CASE_TEMPLATE("ValidConceptMapping test negative", T, std::variant<int, negative>, std::variant<int, positive>, std::variant<E1, int>,
                   std::variant<E1, negative>, std::variant<E1, positive>, std::variant<std::string, std::string_view>,
                   std::variant<std::vector<std::byte>, std::span<const std::byte>>, std::variant<std::byte, std::uint8_t>,
                   std::variant<static_tag<1>, dynamic_tag<uint64_t>>,
                   std::variant<std::map<int, std::string>, std::unordered_map<int, double>>,
                   std::variant<static_tag<1>, static_tag<2>, static_tag<1>>, std::variant<static_tag<2>, E1, B2>,
                   std::variant<float16_t, float, double, bool, std::nullptr_t, simple, int, std::string, std::span<const std::byte>, B2,
                                A2, std::array<int, 5>, std::map<int, std::string>, std::optional<E1>>) {

    auto result    = valid_concept_mapping_v<T>;
    auto array     = valid_concept_mapping_array_v<T>;
    auto unmatched = valid_concept_mapping_n_unmatched_v<T>;
    fmt::print("Array: {}, Expecting <false>: Got: <{}>\n", array, result);

    CHECK(!result);
    CHECK_EQ(unmatched, 0);
}

struct VA1 {
    static constexpr std::uint64_t cbor_tag = 1;
    E1                             e1;
};

struct VA2 {
    static constexpr std::uint64_t cbor_tag = 2;
    E1                             e1;
};

struct VA3 {
    static constexpr std::uint64_t cbor_tag = 3;
    E1                             e1;
};

struct VA4 {
    static constexpr std::uint64_t cbor_tag = 4;
    E1                             e1;
};

TEST_CASE_TEMPLATE("Nested variants positive", T, std::variant<std::variant<VA1, VA2>, std::variant<VA3, VA4>>,
                   std::variant<std::variant<double, std::nullptr_t>, std::variant<bool, float>>) {
    auto result    = valid_concept_mapping_v<T>;
    auto array     = valid_concept_mapping_array_v<T>;
    auto unmatched = valid_concept_mapping_n_unmatched_v<T>;
    fmt::print("Array: {}, Expecting <true>: Got: <{}>\n", array, result);

    CHECK(result);
    CHECK_EQ(unmatched, 0);
}

TEST_CASE_TEMPLATE("Nested variants negative", T, std::variant<std::variant<VA1, VA2>, std::variant<VA3, static_tag<1>>>,
                   std::variant<std::variant<double, float>, std::variant<bool, float>>) {
    auto result    = valid_concept_mapping_v<T>;
    auto array     = valid_concept_mapping_array_v<T>;
    auto unmatched = valid_concept_mapping_n_unmatched_v<T>;
    fmt::print("Array: {}, Expecting <false>: Got: <{}>\n", array, result);

    CHECK(!result);
    CHECK_EQ(unmatched, 0);
}

TEST_CASE_TEMPLATE("Multimaps concept matching", T, std::multimap<int, std::string>, std::unordered_multimap<int, std::string>) {
    static_assert(IsMap<T>);
    static_assert(IsMultiMap<T>);
    static_assert(IsRangeOfCborValues<T>);
    static_assert(!IsFixedArray<T>);
}

TEST_CASE_TEMPLATE("Not multimap concept matching", T, std::map<int, std::string>, std::unordered_map<int, std::string>) {
    static_assert(IsMap<T>);
    static_assert(!IsMultiMap<T>);
    static_assert(IsRangeOfCborValues<T>);
    static_assert(!IsFixedArray<T>);
}