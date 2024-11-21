#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <doctest/doctest.h>
#include <fmt/core.h>
#include <fmt/ranges.h>

using namespace cbor::tags;

enum class E { A, B, C, D };
enum class F : int { A, B, C, D };
enum class G : std::uint8_t { A, B, C, D };

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

    std::variant<G, int> v = -42;

    enc(v);

    auto                 dec = make_decoder(data);
    std::variant<G, int> v2;

    dec(v2);

    CHECK_EQ(v, v2);
}

TEST_CASE("expected integer precedence in variant - TODO: fix cannot compile instead") {
    std::vector<std::byte> data;
    auto                   enc = make_encoder(data);

    std::variant<int, G> v = G::D;

    enc(v);

    auto                 dec = make_decoder(data);
    std::variant<int, G> v2;

    dec(v2);

    // NOTE: NOT EQUAL, int is taking the enum value first, before G can be checked
    CHECK_NE(v.index(), v2.index());
}
