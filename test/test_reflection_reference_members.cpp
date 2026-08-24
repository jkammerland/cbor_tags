#include "test_util.h"

#include <cbor_tags/cbor_concepts.h>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_reflection.h>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

using namespace cbor::tags;

namespace {

struct value_pair {
    int a;
    int b;
};

struct mutable_ref_pair {
    int  a;
    int &b;
};

struct const_ref_pair {
    int        a;
    const int &b;
};

struct mixed_refs {
    int                a;
    int               &b;
    const std::string &c;
};

struct only_ref {
    int &a;
};

struct only_value {
    int a;
};

struct move_only_leaf {
    int a;
};

// The reference probe returns an lvalue, so brace-initializing this member from it would select
// std::vector's copy constructor. That constructor is declared for a move-only element type but
// ill-formed when instantiated, and Clang instantiates it eagerly from inside the requires-clause.
// Counting must therefore never reach the reference probe for an aggregate the value probe
// already resolved.
struct move_only_container {
    std::vector<std::unique_ptr<move_only_leaf>> children;
    int                                          n;
};

} // namespace

TEST_CASE("reflection detects arity of aggregates with reference members") {
    // Regression guard for the root cause: deducing a conversion function template against a
    // target reference type strips the reference, so a by-value probe yields a prvalue that
    // cannot bind to `int &`. `any_ref` returns a reference and binds to both.
    CHECK_FALSE(IsBracesContructible<mutable_ref_pair, any, any>);
    CHECK(IsBracesContructible<mutable_ref_pair, any_ref, any_ref>);

    CHECK(IsBracesContructible<const_ref_pair, any_ref, any_ref>);
    CHECK(IsBracesContructible<value_pair, any_ref, any_ref>);

    // Arity must not be over- or under-counted.
    CHECK_FALSE(IsBracesContructible<mutable_ref_pair, any_ref>);
    CHECK_FALSE(IsBracesContructible<mutable_ref_pair, any_ref, any_ref, any_ref>);
    CHECK_FALSE(IsBracesContructible<value_pair, any_ref, any_ref, any_ref>);

    int               value = 0;
    const std::string text;
    CHECK_EQ(std::tuple_size_v<decltype(to_tuple(mutable_ref_pair{1, value}))>, 2);
    CHECK_EQ(std::tuple_size_v<decltype(to_tuple(const_ref_pair{1, value}))>, 2);
    CHECK_EQ(std::tuple_size_v<decltype(to_tuple(only_ref{value}))>, 1);
    CHECK_EQ(std::tuple_size_v<decltype(to_tuple(mixed_refs{1, value, text}))>, 3);
}

TEST_CASE("counting an aggregate holding a move-only container never reaches the reference probe") {
    CHECK_EQ(detail::aggregate_binding_count<move_only_container>, 2);
    CHECK_EQ(std::tuple_size_v<decltype(to_tuple(std::declval<move_only_container &>()))>, 2);

    // The value probe resolves this type, so the reference probe stays in a discarded branch.
    CHECK(IsBracesContructible<move_only_container, any, any>);
}

TEST_CASE("encode aggregate with a mutable reference member") {
    // Previously encoded zero bytes while reporting success.
    int referenced = 99;

    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);
    REQUIRE(enc(mutable_ref_pair{7, referenced}).has_value());

    // array(2), 7, 99
    CHECK_EQ(to_hex(data), "82071863");

    // A mutable reference member must encode identically to a plain value member.
    std::vector<std::byte> value_data;
    auto                   value_enc = make_encoder(value_data);
    REQUIRE(value_enc(value_pair{7, 99}).has_value());
    CHECK_EQ(to_hex(data), to_hex(value_data));
}

TEST_CASE("encode aggregate with a const reference member") {
    const int referenced = 99;

    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);
    REQUIRE(enc(const_ref_pair{7, referenced}).has_value());
    CHECK_EQ(to_hex(data), "82071863");
}

TEST_CASE("encode aggregate whose only member is a reference") {
    int referenced = 1;

    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);
    REQUIRE(enc(only_ref{referenced}).has_value());

    // A single-member aggregate encodes transparently as its member, with no array header.
    // The reference member must produce exactly what the equivalent value member produces.
    std::vector<std::byte> value_data;
    auto                   value_enc = make_encoder(value_data);
    REQUIRE(value_enc(only_value{1}).has_value());

    CHECK_EQ(to_hex(data), "01");
    CHECK_EQ(to_hex(data), to_hex(value_data));
}

TEST_CASE("encode aggregate mixing value, mutable and const reference members") {
    int               number = 7;
    const std::string text   = "hi";

    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);
    REQUIRE(enc(mixed_refs{1, number, text}).has_value());

    // array(3), 1, 7, text(2) "hi"
    CHECK_EQ(to_hex(data), "830107626869");
}

TEST_CASE("decode into an aggregate with a mutable reference member writes through it") {
    int referenced = 0;

    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);
    REQUIRE(enc(value_pair{7, 99}).has_value());

    mutable_ref_pair decoded{0, referenced};
    auto             dec = make_decoder(data);
    REQUIRE(dec(decoded).has_value());

    CHECK_EQ(decoded.a, 7);
    CHECK_EQ(referenced, 99);
}
