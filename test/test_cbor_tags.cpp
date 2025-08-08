#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_concepts_checking.h"
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
#include <limits>
#include <list>
#include <map>
#include <memory_resource>
#include <nameof.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
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
        CHECK(enc(tag_B));
        s1 = to_hex(data);
    }
    {
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        CHECK(enc(inline_tag_example{-42, "Hello world!"}));
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
        t.cbor_tag.cbor_tag = 140;
    }
    t.s = "Hello world!";

    CHECK(enc(t));

    auto dec = make_decoder(data);
    T    result;
    CHECK(dec(result));
}

TEST_CASE("Test tuple static tag") {
    using namespace literals;
    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);

    auto tag = make_tag_pair(140_tag, std::tuple<int, std::string>{42, "Hello world!"});
    CHECK(enc(tag));

    auto                         dec = make_decoder(data);
    std::tuple<int, std::string> result;
    auto                         tag_result = make_tag_pair(140_tag, result);
    CHECK(dec(tag_result));

    CHECK_EQ(std::get<0>(result), 42);
    CHECK_EQ(std::get<1>(result), "Hello world!");
}

TEST_CASE("Test tuple dynamic tag") {
    using namespace literals;
    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);

    auto tag = make_tag_pair(140_tag, std::tuple<int, std::string>{42, "Hello world!"});
    CHECK(enc(tag));

    fmt::print("data: {}\n", to_hex(data));

    auto dec = make_decoder(data);

    std::tuple<dynamic_tag<std::uint64_t>, int, std::string> result;
    std::get<0>(result).cbor_tag = 140;
    CHECK(dec(result));

    CHECK_EQ(std::get<0>(result), 140);
    CHECK_EQ(std::get<1>(result), 42);
    CHECK_EQ(std::get<2>(result), "Hello world!");
}

TEST_CASE("Nested structs") {
    struct A {
        static_tag<140> cbor_tag;
        int             a;
        struct B {
            dynamic_tag<uint64_t> cbor_tag{5};
            int                   b;
            struct C {
                static_tag<141> cbor_tag;
                int             c;
            } c;
        } b;
    } a{.cbor_tag = {}, .a = 1, .b = {.cbor_tag = {5}, .b = 2, .c = {.cbor_tag = {}, .c = 3}}};

    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);
    CHECK(enc(a));

    auto dec = make_decoder(data);
    A    result;
    CHECK(dec(result));

    CHECK_EQ(a.a, result.a);
    CHECK_EQ(a.b.cbor_tag.cbor_tag, result.b.cbor_tag.cbor_tag);
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

    D d{.cbor_tag = {}, .v = "Hello world!"};
    CHECK(enc(d));

    auto dec = make_decoder(data);
    D    result;
    CHECK(dec(result));

    CHECK_EQ(std::get<std::string>(d.v), std::get<std::string>(result.v));
}

TEST_CASE("Multi tag handling") {
    {
        using namespace literals;
        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        CHECK(enc(141_tag, D{.cbor_tag = {}, .v = "Hello world!"}));

        fmt::print("data: {}\n", to_hex(data));

        auto dec = make_decoder(data);
        D    result;
        CHECK(dec(141_tag, result));

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

        CHECK(enc(MultiObj{.cbor_tag = {}, .a = {.cbor_tag = {}, .a = 1}, .b = {.cbor_tag = {}, .b = 2}}));

        fmt::print("data: {}\n", to_hex(data));

        if (decltype(enc)::options::wrap_groups) {
            REQUIRE_EQ(to_hex(data), "d88c82d88e01d88d02");
        }

        auto     dec = make_decoder(data);
        MultiObj result;
        CHECK(dec(result));

        CHECK_EQ(result.a.a, 1);
        CHECK_EQ(result.b.b, 2);
    }
}

template <size_t N> struct A0 {
    static_tag<N> cbor_tag;
    int           a;
};

using A1 = A0<111>;
using A2 = A0<222>;
using A3 = A0<333>;

TEST_CASE_TEMPLATE("Variant tags", AX, A1, A2, A3) {
    using variant = std::variant<A1, A2, A3>;
    variant v0    = AX{};

    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);
    CHECK(enc(v0));

    auto    dec = make_decoder(data);
    variant v;
    CHECK(dec(v));

    CHECK(std::holds_alternative<AX>(v));
}

template <size_t N> struct DerivedA0 : A0<N> {
    int a;
};
using DerivedA1 = DerivedA0<1>;

TEST_CASE("Derived tags") {
    // This cannot work as far as I know, need manual encoding/decoding
    // auto &&[p1] = DerivedA1{};
}

namespace v1 {
struct Version {
    static_tag<std::numeric_limits<std::uint64_t>::max()> cbor_tag;
    std::variant<A1, A2, A3>                              v;
    double                                                damage;
};
} // namespace v1

TEST_CASE_TEMPLATE("Variant tags in struct", AX, A1, A2, A3) {
    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);

    v1::Version v{{}, AX{}, 3.14};
    CHECK(enc(v));

    auto        dec = make_decoder(data);
    v1::Version result;
    REQUIRE(dec(result));

    CHECK(std::holds_alternative<AX>(result.v));
    CHECK_EQ(result.damage, 3.14);
}

namespace v2 {
struct Version {
    static_tag<141>          cbor_tag;
    std::variant<A1, A2, A3> v;
    double                   damage;
    std::string              name;
};
} // namespace v2

TEST_CASE_TEMPLATE("Nested tagged variant and structs", AX, A1, A2, A3) {
    {
        using VersionVariant = std::variant<v1::Version, v2::Version>;
        using VariantXXX     = std::variant<A1, A2, A3>;
        CHECK_EQ(valid_concept_mapping_n_unmatched_v<VersionVariant>, 0);
        CHECK_EQ(valid_concept_mapping_n_unmatched_v<VariantXXX>, 0);

        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        VersionVariant v{v1::Version{.cbor_tag = {}, .v = AX{.cbor_tag = {}, .a = 2}, .damage = 3.14}};

        CHECK(enc(v));

        fmt::print("data: {}\n", to_hex(data));

        auto           dec = make_decoder(data);
        VersionVariant result;
        auto           status = dec(result);
        if (!status) {
            fmt::print("Error: {}\n", status_message(status.error()));
        }
        REQUIRE(status);
        REQUIRE(std::holds_alternative<v1::Version>(result));
        CHECK_EQ(std::get<v1::Version>(result).damage, 3.14);
    }

    {
        using VersionVariant = std::variant<v1::Version, v2::Version>;

        auto data = std::vector<std::byte>{};
        auto enc  = make_encoder(data);

        VersionVariant v{v2::Version{{}, AX{}, 3.14, "Hello world!"}};
        CHECK(enc(v));

        fmt::print("data: {}\n", to_hex(data));

        auto           dec = make_decoder(data);
        VersionVariant result;
        REQUIRE(dec(result));

        REQUIRE(std::holds_alternative<v2::Version>(result));
        CHECK_EQ(std::get<v2::Version>(result).damage, 3.14);
        CHECK_EQ(std::get<v2::Version>(result).name, "Hello world!");
    }
}

TEST_CASE("Status handling - memory") {
    std::array<std::byte, 1024>         R;
    std::pmr::monotonic_buffer_resource resource(R.data(), R.size(), std::pmr::null_memory_resource());
    std::pmr::vector<std::byte>         buffer(&resource);

    auto enc = make_encoder(buffer);

    struct A {
        static_tag<140> cbor_tag;
        int             a;
    };

    std::vector<A> v(1e3, A{.cbor_tag = {}, .a = 42});
    auto           result = enc(v);
    REQUIRE(!result);
    CHECK_EQ(result.error(), status_code::out_of_memory);
}

TEST_CASE("Status handling - decode wrong tag") {
    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);

    using namespace literals;
    auto result = enc(make_tag_pair(140_tag, A1{}));
    CHECK(result);

    auto dec = make_decoder(data);
    A1   a1;
    auto result2 = dec(make_tag_pair(141_tag, a1));
    CHECK(!result2); // internal error for now
}

TEST_CASE("Advanced tag problem positive") {
    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);

    std::variant<A1, A2, A3> v = A3{};
    REQUIRE(enc(v));

    auto                     dec = make_decoder(data);
    std::variant<A1, A2, A3> result;
    REQUIRE(dec(result));

    CHECK(std::holds_alternative<A3>(result));
}

TEST_CASE("Advanced tag problem negative") {
    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);

    std::variant<A1, A2, A3> v = A3{};
    REQUIRE(enc(v));

    auto                 dec = make_decoder(data);
    std::variant<A1, A2> result;
    auto                 status = dec(result);
    REQUIRE(!status.has_value());
    REQUIRE_EQ(status.error(), status_code::no_match_in_variant_on_buffer);
}

TEST_CASE("Switching instead of variant") {
    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);

    struct F1 {
        int a{42};
    };
    struct F2 {
        std::string s{"43"};
    };
    struct F3 {
        double d{44.0};
    };

    using namespace literals;
    CHECK(enc(wrap_as_array(make_tag_pair(140_tag, F1{}), make_tag_pair(141_tag, F2{}), make_tag_pair(0xFFFF_hex_tag, F3{}))));

    auto dec = make_decoder(data);

    std::optional<as_array_any> arr;
    auto                        result = dec(arr);
    REQUIRE(result);
    for (size_t i = 0; i < arr->size; ++i) {
        as_tag_any any{};
        result = dec(any);
        REQUIRE(result);

        switch (any.tag) {
        case 140: {
            F1 f1;
            result = dec(f1);
            REQUIRE(result);
            CHECK_EQ(f1.a, 42);
            break;
        }
        case 141: {
            F2 f2;
            result = dec(f2);
            REQUIRE(result);
            CHECK_EQ(f2.s, "43");
            break;
        }
        case 0xFFFF: {
            F3 f3;
            result = dec(f3);
            REQUIRE(result);
            CHECK_EQ(f3.d, 44.0);
            break;
        }
        default: CHECK(false);
        }
    }
}

struct MaxTag {
    static_tag<std::numeric_limits<uint64_t>::max()> cbor_tag;
    int                                              a;
};

struct MaxTag2 {
    static constexpr uint64_t cbor_tag = std::numeric_limits<uint64_t>::max();
    int                       a;
};

namespace v3 {
struct Version {
    static_tag<std::numeric_limits<std::uint64_t>::max()> cbor_tag;
    std::variant<A1, A2, A3>                              v;
    double                                                damage;
    int                                                   a;
    std::string                                           s;
};
} // namespace v3

TEST_CASE("Max tag") {
    {
        auto tag = detail::get_tag_from_any<MaxTag>();
        static_assert(tag == std::numeric_limits<uint64_t>::max());

        constexpr auto tag2 = detail::get_tag_from_any<MaxTag2>();
        static_assert(tag2 == std::numeric_limits<uint64_t>::max());

        auto tagged3 = std::make_tuple(static_tag<std::numeric_limits<uint64_t>::max()>{}, MaxTag{}, 1);
        static_assert(IsTaggedTuple<decltype(tagged3)>);

        constexpr auto tag4 = detail::get_tag_from_any<v3::Version>();
        static_assert(tag4 == std::numeric_limits<uint64_t>::max());

        auto tagged5 = std::make_tuple(static_tag<std::numeric_limits<uint64_t>::max()>{}, v3::Version{}, 1);
        static_assert(IsTaggedTuple<decltype(tagged5)>);
    }

    {
        // This was used to debug incorrect variant check. Edge case caused "max uint64_t == -1";
        using V3 = v3::Version;
        using V2 = v2::Version;
        auto a   = valid_concept_mapping_n_unmatched_v<std::variant<V2, V3>>;
        CHECK_EQ(a, 0);
    }
}
