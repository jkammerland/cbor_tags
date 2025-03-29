#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "test_util.h"

#include <doctest/doctest.h>
#include <fmt/base.h>
#include <limits>
#include <optional>
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

    fmt::print("{}\n", to_hex(buffer));
    CHECK_EQ(to_hex(buffer), "82db0fffffffffffffff82fb4012000000000000826568656c6c6f65776f726c64fb4000000000000000");

    auto dec = make_decoder(buffer);
    static_assert(IsClassWithDecodingOverload<decltype(dec), decltype(c1)>);
    Class4 c2;
    REQUIRE(dec(c2));
    CHECK_EQ(c1, c2);
}

} // namespace test_classes