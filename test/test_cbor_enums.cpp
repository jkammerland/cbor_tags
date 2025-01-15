#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_integer.h"
#include "cbor_tags/variant_handling.h"
#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cstdint>
#include <doctest/doctest.h>
#include <fmt/core.h>
#include <fmt/ranges.h>

using namespace cbor::tags;

enum class E : uint16_t { A, B, C, D };
enum class F : int { A = -5, B = -2, C = 0, D = 1 };
enum class G : std::uint8_t { A, B, C, D };
enum class H : std::int8_t {};

struct Extra {
    static_tag<4> cbor_tag;
    std::string   s;
};

struct S {
    static constexpr std::size_t cbor_tag = 0x01;
    E                            e;
    F                            f;
    G                            g;
    H                            h;
    Extra                        extra;

    struct Extra2 {
        dynamic_tag<std::uint64_t> cbor_tag{555};
        H                          h;
    };

    Extra2 extra2;
};

TEST_CASE("CBOR - Enum class") {
    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);

    enc(E::A);
    enc(F::B);
    enc(G::C);

    auto dec = make_decoder(data);
    E    e;
    F    f;
    G    g;
    dec(e, f, g);

    CHECK_EQ(e, E::A);
    CHECK_EQ(f, F::B);
    CHECK_EQ(g, G::C);
}

TEST_CASE("CBOR - optional enum") {
    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);

    std::optional<E> e = E::A;
    std::optional<F> f = F::B;
    std::optional<G> g = std::nullopt;

    enc(e);
    enc(f);
    enc(g);

    auto             dec = make_decoder(data);
    std::optional<E> e2;
    std::optional<F> f2;
    std::optional<G> g2;
    dec(e2);
    dec(f2);
    dec(g2);

    CHECK_EQ(e, e2);
    CHECK_EQ(f, f2);
    CHECK_EQ(g, g2);
}

TEST_CASE("CBOR variant enum") {
    {
        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);

        std::variant<F, std::string> v = F::D;
        enc(v);

        auto                         dec = make_decoder(data);
        std::variant<F, std::string> v2;
        dec(v2);

        CHECK_EQ(v, v2);
    }

    {
        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);

        std::variant<E, std::string> v = "Hello world!";
        enc(v);

        auto                         dec = make_decoder(data);
        std::variant<E, std::string> v2;
        dec(v2);

        CHECK_EQ(v, v2);
    }
}

TEST_CASE("CBOR variant enum + negative") {
    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);

    std::variant<G, negative> v = negative{42};

    enc(v);

    fmt::print("buffer: {}\n", to_hex(data));

    auto                      dec = make_decoder(data);
    std::variant<G, negative> v2;

    dec(v2);

    REQUIRE_EQ(v.index(), v2.index());
    CHECK_EQ(std::get<negative>(v).value, std::get<negative>(v2).value);
    fmt::print("v: {} / v2: {} / v: {} / v2: {}\n", v.index(), v2.index(), std::get<negative>(v).value, std::get<negative>(v2).value);
}

TEST_CASE("Check variant for enums static_assert") {
    // std::vector<std::byte> data;

    // constexpr auto Unsigned = [](IsUnsignedOrEnum auto) {};
    // constexpr auto Signed   = [](IsSignedOrEnum auto) {};

    // static_assert(!valid_concept_mapping_v<std::variant<int, G>, Unsigned, Signed>, "Expected int to be signed");
    // static_assert(!valid_concept_mapping_v<std::variant<G, int>, Unsigned, Signed>, "Expected int to be signed");
    // static_assert(!valid_concept_mapping_v<std::variant<uint16_t, G>, Unsigned, Signed>, "Expected uint16_t to be unsigned");
}

TEST_CASE("CBOR - struct with enum") {
    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);

    S s{.e      = E::A,
        .f      = F::B,
        .g      = G::C,
        .h      = H{static_cast<H>(255)},
        .extra  = {.cbor_tag = {}, .s = "Hello"},
        .extra2 = {.cbor_tag = {}, .h = static_cast<H>(-1)}};
    enc(s);

    auto dec = make_decoder(data);
    S    s2;
    dec(s2);

    CHECK_EQ(s.e, s2.e);
    CHECK_EQ(s.f, s2.f);
    CHECK_EQ(s.g, s2.g);
    CHECK_EQ(s.h, s2.h);
    CHECK_EQ(static_cast<uint8_t>(s.h), 255);
    CHECK_EQ(s.extra.s, s2.extra.s);
    CHECK_EQ(static_cast<int8_t>(s.extra2.h), -1);
}

struct TEST0 {
    static constexpr std::size_t cbor_tag = 0x01;
    int                          e;
};

TEST_CASE("CBOR - struct with enum") {
    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);

    std::optional<TEST0> s = TEST0{1};
    enc(s);

    auto                 dec = make_decoder(data);
    std::optional<TEST0> s2;
    dec(s2);

    CHECK_EQ(s->e, s2->e);
}

TEST_CASE("CBOR - struct with enum + optional") {
    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);

    S                s{.e      = E::A,
                       .f      = F::B,
                       .g      = G::C,
                       .h      = H{static_cast<H>(255)},
                       .extra  = {.cbor_tag = {}, .s = "Hello"},
                       .extra2 = {.cbor_tag = {555}, .h = static_cast<H>(-1)}};
    std::optional<S> os = s;
    REQUIRE(enc(os));

    auto             dec = make_decoder(data);
    std::optional<S> os2;
    auto             status = dec(os2);
    REQUIRE(status);

    CHECK_EQ(os->e, os2->e);
    CHECK_EQ(os->f, os2->f);
    CHECK_EQ(os->g, os2->g);
    CHECK_EQ(os->h, os2->h);
    CHECK_EQ(static_cast<uint8_t>(os->h), 255);
    CHECK_EQ(os->extra.s, os2->extra.s);
    CHECK_EQ(static_cast<int8_t>(os->extra2.h), -1);
}

TEST_CASE("CBOR - struct with enum + variant") {
    {
        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);

        S                            s{.e      = E::A,
                                       .f      = F::B,
                                       .g      = G::C,
                                       .h      = H{static_cast<H>(255)},
                                       .extra  = {.cbor_tag = {}, .s = "Hello"},
                                       .extra2 = {.cbor_tag = {555}, .h = static_cast<H>(-1)}};
        std::variant<S, std::string> v = s;
        enc(v);

        auto                         dec = make_decoder(data);
        std::variant<S, std::string> v2;
        dec(v2);

        CHECK_EQ(v.index(), v2.index());
        const auto &s1 = std::get<S>(v);
        const auto &s2 = std::get<S>(v2);
        CHECK_EQ(s1.e, s2.e);
        CHECK_EQ(s1.f, s2.f);
        CHECK_EQ(s1.g, s2.g);
        CHECK_EQ(s1.h, s2.h);
        CHECK_EQ(static_cast<uint8_t>(s1.h), 255);
        CHECK_EQ(s1.extra.s, s2.extra.s);
        CHECK_EQ(static_cast<int8_t>(s1.extra2.h), -1);
    }

    {
        std::vector<std::byte> data;
        auto                   enc = make_encoder(data);

        S                            s{.e      = E::A,
                                       .f      = F::B,
                                       .g      = G::C,
                                       .h      = H{static_cast<H>(255)},
                                       .extra  = {.cbor_tag = {}, .s = "Hello"},
                                       .extra2 = {.cbor_tag = {}, .h = static_cast<H>(-1)}};
        std::variant<S, std::string> v = "Hello world!";
        enc(v);

        auto                         dec = make_decoder(data);
        std::variant<S, std::string> v2;
        dec(v2);

        CHECK_EQ(v.index(), v2.index());
        CHECK_EQ(std::get<std::string>(v), std::get<std::string>(v2));
    }
}