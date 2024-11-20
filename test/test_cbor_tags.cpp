#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_reflection.h"
#include "cbor_tags/float16_ieee754.h"
#include "test_util.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <exception>
#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <fmt/std.h>
#include <format>
#include <list>
#include <map>
#include <nameof.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using namespace cbor::tags;

struct B {
    std::int64_t a;
    std::string  s;
};
struct inline_tag_example {
    static constexpr std::uint64_t cbor_tag = 140;
    B                              b;
};

TEST_CASE("Basic tag") {
    using namespace literals;
    std::string s1, s2;
    {
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        auto tag_B = make_tag_pair(140_tag, B{-42, "Hello world!"});
        enc(tag_B);
        s1 = to_hex(data);
    }
    {
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        enc(inline_tag_example{-42, "Hello world!"});
        s2 = to_hex(data);
    }

    CHECK_EQ(s1, s2);
}

struct STATIC_EX1 {
    static_tag<140u>           cbor_tag;
    std::optional<std::string> s;
};

struct DYNAMIC_EX1 {
    dynamic_tag<uint64_t>      cbor_tag;
    std::optional<std::string> s;
};

struct INLINE_EX1 {
    static constexpr std::uint64_t cbor_tag = 140;
    std::optional<std::string>     s;
};

TEST_CASE_TEMPLATE("Test tag 140", T, STATIC_EX1, DYNAMIC_EX1, INLINE_EX1) {
    using namespace std::string_view_literals;

    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);

    T t;
    if constexpr (std::is_same_v<T, DYNAMIC_EX1>) {
        t.cbor_tag.value = 140;
    }
    t.s = "Hello world!";

    enc(t);

    auto dec = make_decoder(data);
    T    result;
    dec(result);
}

TEST_CASE("Test tuple static tag") {
    using namespace literals;
    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);

    auto tag = make_tag_pair(140_tag, std::tuple<int, std::string>{42, "Hello world!"});
    enc(tag);

    auto                         dec = make_decoder(data);
    std::tuple<int, std::string> result;
    auto                         tag_result = make_tag_pair(140_tag, result);
    dec(tag_result);

    CHECK_EQ(std::get<0>(result), 42);
    CHECK_EQ(std::get<1>(result), "Hello world!");
}

TEST_CASE("Test tuple dynamic tag") {
    using namespace literals;
    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);

    auto tag = make_tag_pair(140_tag, std::tuple<int, std::string>{42, "Hello world!"});
    enc(tag);

    auto dec = make_decoder(data);

    std::tuple<dynamic_tag<std::uint64_t>, int, std::string> result;
    dec(result);

    CHECK_EQ(std::get<0>(result), 140);
    CHECK_EQ(std::get<1>(result), 42);
    CHECK_EQ(std::get<2>(result), "Hello world!");
}

TEST_CASE("Nested structs") {
    struct A {
        static_tag<140> cbor_tag;
        int             a;
        struct B {
            dynamic_tag<uint64_t> cbor_tag;
            int                   b;
            struct C {
                static_tag<141> cbor_tag;
                int             c;
            } c;
        } b;
    } a{{}, 1, {{5}, 2, {{}, 3}}};

    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);
    enc(a);

    auto dec = make_decoder(data);
    A    result;
    dec(result);

    CHECK_EQ(a.a, result.a);
    CHECK_EQ(a.b.cbor_tag.value, result.b.cbor_tag.value);
    CHECK_EQ(a.b.b, result.b.b);
    CHECK_EQ(a.b.c.c, result.b.c.c);
}

struct D {
    static_tag<140>                                       cbor_tag;
    std::variant<int, std::string, std::map<int, double>> v;
};

TEST_CASE("Variant") {
    using namespace literals;
    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);

    D d{{}, "Hello world!"};
    enc(d);

    auto dec = make_decoder(data);
    D    result;
    dec(result);

    CHECK_EQ(std::get<std::string>(d.v), std::get<std::string>(result.v));
}

TEST_CASE("Multi tag handling") {
    {
        using namespace literals;
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        enc(141_tag, D{{}, "Hello world!"});

        fmt::print("data: {}\n", to_hex(data));

        auto dec = make_decoder(data);
        D    result;
        dec(141_tag, result);

        CHECK_EQ(std::get<std::string>(result.v), "Hello world!");
    }

    struct MultiObj {
        static_tag<140> cbor_tag;

        struct A {
            static_tag<142> cbor_tag;
            int             a;
        } a;

        struct B {
            static_tag<141> cbor_tag;
            int             b;
        } b;
    };

    {
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        enc(MultiObj{{}, {{}, 1}, {{}, 2}});

        fmt::print("data: {}\n", to_hex(data));
        REQUIRE_EQ(to_hex(data), "d88cd88e01d88d02");

        auto     dec = make_decoder(data);
        MultiObj result;
        dec(result);

        CHECK_EQ(result.a.a, 1);
        CHECK_EQ(result.b.b, 2);
    }
}
