
#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_decoder.h"
#include "test_util.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <doctest/parts/doctest_fwd.h>
#include <fmt/core.h>
#include <list>
#include <memory_resource>
#include <nameof.hpp>
#include <optional>
#include <utility>
#include <variant>

using namespace cbor::tags;
using namespace std::string_view_literals;

using variant_type = std::variant<uint64_t, double, std::string>;

TEST_CASE_TEMPLATE("CBOR Decoder", T, std::vector<char>, std::deque<std::byte>, std::list<uint8_t>) {

    using value_type = typename T::value_type;
    auto &&bytes     = to_bytes<value_type>("010203"sv);
    T      data(bytes.begin(), bytes.end());

    auto dec = make_decoder(data);

    for (const auto &value : bytes) {
        variant_type result;
        dec(result);
        CHECK_EQ(std::holds_alternative<uint64_t>(result), true);
        CHECK_EQ(std::get<uint64_t>(result), static_cast<uint64_t>(value));
    }
}

TEST_CASE_TEMPLATE("CBOR decode from array", T, std::array<unsigned char, 8>, std::deque<char>) {
    T data = {0x01, 0x02, 0x03, 0x04, 0x05};

    auto dec = make_decoder(data);

    for (const auto &value : data) {
        variant_type result;
        dec(result);
        CHECK_EQ(std::holds_alternative<uint64_t>(result), true);
        CHECK_EQ(std::get<uint64_t>(result), static_cast<uint64_t>(value));
    }
}

TEST_CASE_TEMPLATE("Test decode dynamic tag 1", T, std::vector<uint8_t>, std::deque<uint8_t>, std::list<uint8_t>) {
    using namespace std::string_view_literals;
    auto bytes = to_bytes("c16c48656c6c6f20776f726c6421"sv);

    auto dec = make_decoder(bytes);
    struct A {
        dynamic_tag<std::uint64_t> cbor_tag{1};
        std::string                b;
    };

    static_assert(HasDynamicTag<A>);
    static_assert(IsTag<A>);

    A a;
    auto &[tag, value_a] = a;

    dec(a.cbor_tag, value_a);
    CHECK_EQ(tag, 1);
    CHECK_EQ(value_a, "Hello world!");
}

TEST_CASE_TEMPLATE("Test decode static tag 1", T, std::vector<uint8_t>, std::deque<uint8_t>, std::list<uint8_t>) {
    using namespace std::string_view_literals;
    auto bytes = to_bytes("c16c48656c6c6f20776f726c6421"sv);

    auto dec = decoder<decltype(bytes), Options<default_expected /*, default_wrapping*/>, cbor_header_decoder>(bytes);
    struct A {
        static_tag<1> cbor_tag;
        std::string   b;
    };

    static_assert(HasStaticTag<A>);
    static_assert(IsTag<A>);

    A a;
    auto &[tag, value_a] = a;

    dec(a);
    CHECK_EQ(tag, 1);
    CHECK_EQ(value_a, "Hello world!");
}

struct STATICTAGINLINE {
    static constexpr uint64_t cbor_tag = 1;
    std::string               b;
};

TEST_CASE_TEMPLATE("Test decode static tag 1 reverse", T, std::vector<uint8_t>, std::deque<char>, std::list<char>) {
    using namespace std::string_view_literals;
    auto bytes = to_bytes<typename T::value_type>("c16c48656c6c6f20776f726c6421"sv);

    auto dec = make_decoder(bytes);

    static_assert(HasInlineTag<STATICTAGINLINE>);
    static_assert(IsTag<STATICTAGINLINE>);

    STATICTAGINLINE a;
    auto &[s] = a;

    dec(a);
    CHECK_EQ(s, "Hello world!");
}

TEST_CASE_TEMPLATE("Test decode tag 1 optional", T, std::vector<char>, std::deque<uint8_t>, std::list<std::byte>) {
    {
        using namespace std::string_view_literals;
        auto bytes = to_bytes<typename T::value_type>("c16c48656c6c6f20776f726c6421"sv);

        auto dec = make_decoder(bytes);
        struct A {
            std::optional<std::string> b;
        };

        auto a               = make_tag_pair(static_tag<1>{}, A{});
        auto &[tag, value_a] = a;

        dec(a);
        CHECK_EQ(value_a.b, "Hello world!");
    }
    {
        using namespace std::string_view_literals;
        auto bytes = to_bytes("c1f6"sv);

        auto dec = make_decoder(bytes);

        struct A {
            std::optional<std::string> b;
        };

        auto a               = make_tag_pair(static_tag<1>{}, A{});
        auto &[tag, value_a] = a;
        dec(a);
        CHECK_EQ(value_a.b, std::nullopt);
    }
}

template <typename MajorType, typename... T> bool contains_major(MajorType major, std::variant<T...>) {
    return (... || (major == ConceptType<MajorType, T>::value));
}

TEST_CASE_TEMPLATE("Test decode tag 1 variant", T, std::vector<char>, std::deque<uint8_t>, std::list<std::byte>) {
    {
        using namespace std::string_view_literals;
        auto bytes = to_bytes("c16c48656c6c6f20776f726c6421"sv);

        auto dec = make_decoder(bytes);
        struct A {
            std::variant<std::string, int> b;
        };

        auto a               = make_tag_pair(static_tag<1>{}, A{});
        auto &[tag, value_a] = a;
        value_a.b            = 4;

        REQUIRE(contains_major(static_cast<T::value_type>(3), value_a.b));

        dec(a);
        REQUIRE(std::holds_alternative<std::string>(value_a.b));
        CHECK_EQ(std::get<std::string>(value_a.b), "Hello world!");
    }
    {
        using namespace std::string_view_literals;
        auto bytes = to_bytes("c1f6"sv);

        auto dec = make_decoder(bytes);

        struct A {
            std::optional<std::string> b;
        };

        auto a               = make_tag_pair(static_tag<1>{}, A{});
        auto &[tag, value_a] = a;
        dec(a);
        CHECK_EQ(value_a.b, std::nullopt);
    }
}