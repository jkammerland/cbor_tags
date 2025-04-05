#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/cbor_visualization.h"
#include "test_util.h"

#include <cassert>
#include <cstddef>
#include <doctest/doctest.h>
#include <fmt/base.h>
#include <fmt/format.h>
#include <iostream>
#include <map>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

using namespace cbor::tags;

/* Below is some example mappings of STL types to cbor major types */
struct AllCborMajorsExample {
    // static constexpr std::uint64_t cbor_tag = 123;

    positive                   a0; // Major type 0 (unsigned integer)
    negative                   a1; // Major type 1 (negative integer)
    int                        a;  // Major type 0 or 1 (unsigned or negative integer)
    std::string                b;  // Major type 2 (text string)
    std::vector<std::byte>     c;  // Major type 3 (byte string)
    std::vector<int>           d;  // Major type 4 (array)
    std::map<int, std::string> e;  // Major type 5 (map)
    struct B {
        static_tag<1337> cbor_tag; // Major type 6 (tag)
        bool             a;        // Major type 7 (simple value)
    } f;
    double g; // Major type 7 (float)

    // More advanced types
    std::variant<int, std::string, std::vector<int>> h; // Major type 0, 1, 2 or 4 (array can take major type 0 or 1)
    std::unordered_multimap<std::string, std::variant<int, std::map<std::string, double>, std::vector<float>>> i; // Major type 5 (map) ...
    std::optional<std::map<int, std::string>> j; // Major type 5 (map) or 7 (simple value std::nullopt)
};

TEST_CASE("CBOR - Advanced types") {
    // Encoding
    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);

    auto a0 = AllCborMajorsExample{
        .a0 = 42,  // +42
        .a1 = 42,  // -42 (implicit conversion to negative)
        .a  = -42, // -42 (integer could be +/-)
        .b  = "Hello, World!",
        .c  = {std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}},
        .d  = {1, 2, 3, 4, 5},
        .e  = {{1, "one"}, {2, "two"}, {3, "three"}},
        .f  = {.cbor_tag = {}, .a = true},
        .g  = 3.14,
        .h  = "Hello, World!",
        .i  = {{"one", 1}, {"two", std::map<std::string, double>({{"0", 1.0}, {"1", 1.1}})}, {"one", std::vector<float>{0.0f, 1.0f, 2.0f}}},
        .j  = std::nullopt};

    auto result = enc(a0);
    if (!result) {
        std::cerr << "Failed to encode A" << '\n';
        CHECK(false);
    }

    fmt::print("Encoded: {}\n", to_hex(data));

    // Decoding
    auto                 dec = make_decoder(data);
    AllCborMajorsExample a1;
    result = dec(a1);
    if (!result) {
        std::cerr << "Failed to decode A: " << status_message(result.error()) << '\n';
        CHECK(false);
    }

    CHECK_EQ(a0.a0, a1.a0);
    CHECK_EQ(a0.a1, a1.a1);
    CHECK_EQ(a0.a, a1.a);
    CHECK_EQ(a0.b, a1.b);
    CHECK_EQ(a0.c, a1.c);
    CHECK_EQ(a0.d, a1.d);
    CHECK_EQ(a0.e, a1.e);
    CHECK_EQ(a0.f.a, a1.f.a);
    CHECK_EQ(a0.g, a1.g);
    CHECK_EQ(a0.h, a1.h);
    CHECK_EQ(a0.i, a1.i);
    CHECK_EQ(a0.j, a1.j);
}

TEST_CASE("Simple ex0") {
    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);
    using namespace std::string_view_literals;
    enc(2, 3.14, "Hello, World!"sv);

    // Decoding
    auto        dec = make_decoder(data);
    int         a;
    double      b;
    std::string c;
    dec(a, b, c);

    assert(a == 2);
    assert(b == 3.14);
    assert(c == "Hello, World!");
}

struct Api1 {
    int a;
    int b;
};

struct Api2 {
    std::string a;
    std::string b;
};

TEST_CASE("switch on tag") {
    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);

    using namespace cbor::tags::literals;
    // Encode Api1 with a tag of 0x10
    REQUIRE(enc(make_tag_pair(10_hex_tag, Api1{.a = 42, .b = 43})));

    // Encode a binary string in the middle of the buffer [the buffer itself]
    REQUIRE(enc(std::vector<std::byte>{}));

    // Encode Api2 with a tag of 0x20
    REQUIRE(enc(make_tag_pair(20_hex_tag, Api2{"hello", "world"})));

    fmt::print("Data: {}\n", to_hex(data));
    fmt::memory_buffer buffer;
    buffer_annotate(data, buffer);
    fmt::print("Annotation: \n{}\n", fmt::to_string(buffer));

    auto                                             dec = make_decoder(data);
    std::variant<std::vector<std::byte>, as_tag_any> value;

    auto visitor = [&dec](auto &&value) {
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, as_tag_any>) {
            if (value.tag == 0x10) {
                Api1 a;
                auto result = dec(a);
                REQUIRE(result);
                if (result) {
                    fmt::print("Api1: a={}, b={}\n", a.a, a.b);
                }
            } else if (value.tag == 0x20) {
                Api2 a;
                auto result = dec(a);
                REQUIRE(result);
                if (result) {
                    fmt::print("Api2: a={}, b={}\n", a.a, a.b);
                }
            } else {
                fmt::print("Unknown tag: {}\n", value.tag);
            }
        } else {
            fmt::print("Binary data: {}\n", to_hex(value));
        }
    };

    REQUIRE(dec(value));
    std::visit(visitor, value);

    REQUIRE(dec(value));
    std::visit(visitor, value);

    REQUIRE(dec(value));
    std::visit(visitor, value);
}

TEST_CASE("Annotation and diagnostics example") {
    // Data vector of a CWT token
    std::vector<std::byte> data =
        to_bytes("d28443a10126a104524173796d6d657472696345434453413235365850a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b770"
                 "37818636f61703a2f2f6c696768742e6578616d706c652e636f6d041a5612aeb0051a5610d9f0061a5610d9f007420b7158405427c1ff28d23fbad1f2"
                 "9c4c7c6a555e601d6fa29f9179bc3d7438bacaca5acd08c8d4d4f96131680c429a01f85951ecee743a52b9b63632c57209120e1c9e30");

    // Annotate the data vector
    fmt::memory_buffer buffer;
    buffer_annotate(data, buffer);
    fmt::format_to(std::back_inserter(buffer), "\n --- \n");

    // Diagnostic notation of the data vector
    buffer_diagnostic(data, buffer, {});
    fmt::format_to(std::back_inserter(buffer), "\n --- \n");

    // Take the payload map (unwrapping the 3rd bstr element) and make it into diagnostic notation too
    data = to_bytes("a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b77037818636f61703a2f2f6c696768742e6578616d706c652e6"
                    "36f6d041a5612aeb0051a5610d9f0061a5610d9f007420b71");

    buffer_diagnostic(data, buffer, {});
    fmt::print("\n{}\n", fmt::to_string(buffer));
}