#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_concepts_checking.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "test_util.h"

#include <doctest/doctest.h>
#include <fmt/base.h>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

using namespace cbor::tags;
using namespace std::string_view_literals;

namespace test_classes {

struct Class3 {
  public:
    Class3() = default;
    explicit Class3(std::variant<int, double> v, std::vector<std::string> s) : value(v), strings(std::move(s)) {}
    Class3(const Class3 &)            = default;
    Class3(Class3 &&)                 = default;
    Class3 &operator=(const Class3 &) = default;
    Class3 &operator=(Class3 &&)      = default;

    bool operator==(const Class3 &) const = default;

  private:
    std::variant<int, double> value;
    std::vector<std::string>  strings;

    // This function is only needed when the class is one of the types in a variant.
    static constexpr uint64_t cbor_tag() { return std::numeric_limits<uint64_t>::max() / 16; }

    friend cbor::tags::Access;
    template <typename Encoder> constexpr auto encode(Encoder &enc) const {
        return enc(static_tag<cbor_tag()>{}, as_array{2}, value, strings);
    }
    template <typename Decoder> constexpr auto decode(Decoder &dec) { return dec(static_tag<cbor_tag()>{}, as_array{2}, value, strings); }
};

TEST_CASE("Test classes") {
    auto buffer = std::vector<uint8_t>{};
    auto enc    = make_encoder(buffer);

    Class3 c1{4.5, {"hello", "world"}};
    static_assert(IsClassWithEncodingOverload<decltype(enc), decltype(c1)>);
    REQUIRE(enc(c1));

    fmt::print("buffer: {}\n", to_hex(buffer));

    auto   dec = make_decoder(buffer);
    Class3 c2;
    static_assert(IsClassWithDecodingOverload<decltype(dec), decltype(c2)>);
    REQUIRE(dec(c2));
    CHECK_EQ(c2, c1);
}

struct Class4 {
    std::optional<Class3> c0;
    double                d_;

    bool operator==(const Class4 &other) const = default;

    Class4() = default;
    Class4(Class3 &&c, double d) : c0(std::move(c)), d_{d} {}
};

// template <typename Encoder> constexpr auto encode(Encoder &enc, const Class4 &c) { return enc(wrap_as_array{c.c0, c.d_}); }

template <typename Transcoder> constexpr auto transcode(Transcoder &tc, Class4 &&c) { return tc(wrap_as_array{c.c0, c.d_}); }
template <typename Transcoder> constexpr auto transcode(Transcoder &tc, const Class4 &c) { return tc(wrap_as_array{c.c0, c.d_}); }

TEST_CASE("Test classes with optional") {
    auto buffer = std::vector<uint8_t>{};
    auto enc    = make_encoder(buffer);

    Class4 c1{Class3{4.5, {"hello", "world"}}, 2.0};
    static_assert(IsClassWithEncodingOverload<decltype(enc), decltype(c1)>);
    REQUIRE(enc(c1));

    CHECK_EQ(to_hex(buffer), "82db0fffffffffffffff82fb4012000000000000826568656c6c6f65776f726c64fb4000000000000000");

    auto dec = make_decoder(buffer);
    static_assert(IsClassWithDecodingOverload<decltype(dec), decltype(c1)>);
    Class4 c2;
    REQUIRE(dec(c2));
    CHECK_EQ(c1, c2);
}

struct Class5 {
    std::optional<Class4> c0;
    double                d_;

    bool operator==(const Class5 &other) const = default;

    Class5() = default;
    Class5(Class4 &&c, double d) : c0(std::move(c)), d_{d} {}

  private:
    friend cbor::tags::Access;
    template <typename Decoder> constexpr auto decode(Decoder &dec) { return dec(wrap_as_array{c0, d_}); }
};

template <typename Encoder> constexpr auto encode(Encoder &enc, const Class5 &c) { return enc(wrap_as_array{c.c0, c.d_}); }

TEST_CASE("Test classes nested free and member functions") {
    auto buffer = std::vector<uint8_t>{};
    auto enc    = make_encoder(buffer);

    Class5 c1{Class4{Class3{4.5, {"hello", "world"}}, 2.0}, 3.0};
    static_assert(IsClassWithEncodingOverload<decltype(enc), decltype(c1)>);
    REQUIRE(enc(c1));

    CHECK_EQ(to_hex(buffer), "8282db0fffffffffffffff82fb4012000000000000826568656c6c6f65776f726c64fb4000000000000000fb4008000000000000");

    auto dec = make_decoder(buffer);
    static_assert(IsClassWithDecodingOverload<decltype(dec), decltype(c1)>);
    Class5 c2;
    REQUIRE(dec(c2));
    CHECK_EQ(c1, c2);
}

struct Class6 {
    std::optional<Class4> c0;
    double                d_;
    bool                  operator==(const Class6 &other) const = default;

    constexpr Class6() = default;
    constexpr Class6(Class4 &&c, double d) : c0(std::move(c)), d_{d} {}
    constexpr ~Class6() = default;

  private:
    friend cbor::tags::Access;
    // static_tag<12> cbor_tag;

    template <typename Encoder> constexpr auto encode(Encoder &enc) const { return enc(wrap_as_array{c0, d_}); }
};
} // namespace test_classes

// TODO: cause build fail on clang-cl windows
// constexpr auto cbor_tag(const Class6 &) { return static_tag<12>{}; }
namespace cbor::tags {
template <> constexpr auto cbor_tag<test_classes::Class6>() { return static_tag<12>{}; }
} // namespace cbor::tags

namespace test_classes {

template <typename Decoder> constexpr auto decode(Decoder &dec, Class6 &&c) { return dec(wrap_as_array{c.c0, c.d_}); }

TEST_CASE("Test classes nested free and member functions - reverse") {
    auto buffer = std::vector<uint8_t>{};
    auto enc    = make_encoder(buffer);

    Class6 c1(Class4(), 3.2);
    REQUIRE(enc(c1));

    CHECK_EQ(to_hex(buffer), "cc8282f6fb0000000000000000fb400999999999999a");

    auto   dec = make_decoder(buffer);
    Class6 c2;
    REQUIRE(dec(c2));
    CHECK_EQ(c1, c2);
}

struct X {
    enum class Enum { A, B, C };
    static_tag<555> cbor_tag;
    Enum            e1{Enum::B};
    bool            operator==(const X &other) const = default;
};

// constexpr auto cbor_tag(const X &) { return static_tag<55555>{}; }

TEST_CASE_TEMPLATE("Class with tag", T, static_tag<1>, static_tag<2>, Class6) {
    std::variant<static_tag<1>, static_tag<2>, Class6, X> v = T{};
    if constexpr (std::is_same_v<T, X>) {
        v = X{.e1 = X::Enum::C};
    }

    auto buffer = std::vector<uint8_t>{};
    auto enc    = make_encoder(buffer);

    REQUIRE(enc(v));

    auto                                                  dec = make_decoder(buffer);
    std::variant<static_tag<1>, static_tag<2>, Class6, X> v2;
    REQUIRE(dec(v2));
    CHECK_EQ(v, v2);
}

template <std::uint64_t N> struct F {
    static constexpr std::uint64_t cbor_tag{N};
    int                            a;
};

template <std::uint64_t N> struct Class7 {
    std::optional<Class4> c0;
    double                d_;

    bool operator==(const Class7 &other) const = default;

    Class7() = default;
    Class7(Class4 &&c, double d) : c0(std::move(c)), d_{d} {}

  private:
    friend cbor::tags::Access;
    template <typename Encoder> constexpr auto encode(Encoder &enc) const { return enc(wrap_as_array{c0, d_}); }
    template <typename Decoder> constexpr auto decode(Decoder &dec) { return dec(wrap_as_array{c0, d_}); }
    static_tag<N>                              cbor_tag;
};

template <std::uint64_t N> class Class8 {
  public:
    int a;

  private:
    friend cbor::tags::Access;
    template <typename Encoder> constexpr auto encode(Encoder &enc) const { return enc(a); }
    template <typename Decoder> constexpr auto decode(Decoder &dec) { return dec(a); }

    static constexpr std::uint64_t cbor_tag{N};
};

TEST_CASE("Test class8 for static inline tag") { static_assert(HasTagMember<Class8<12>>); }

TEST_CASE_TEMPLATE("Class with variant tag collision", T, std::variant<F<12>, static_tag<12>>, std::variant<F<12>, Class6>,
                   std::variant<Class6, Class7<12>>) {
    fmt::println("Testing: {}", valid_concept_mapping_array_v<T>);
    CHECK(!valid_concept_mapping_v<T>);
}

TEST_CASE_TEMPLATE("Class with variant tag NO collision", T, std::variant<F<11>, static_tag<12>>, std::variant<F<11>, Class6>,
                   std::variant<Class6, Class7<13>>) {
    fmt::println("Testing: {}", valid_concept_mapping_array_v<T>);
    CHECK(valid_concept_mapping_v<T>);
}

} // namespace test_classes