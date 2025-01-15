#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_integer.h"
#include "test_util.h"

#include <cassert>
#include <doctest/doctest.h>
#include <fmt/base.h>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

using namespace cbor::tags;

struct A {
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

TEST_CASE("CBOR - Advanced types" * doctest::skip()) {
    using namespace cbor::tags;

    // Encoding
    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);

    auto a0 =
        A{.a0 = 42,
          .a1 = 42,
          .a  = 42,
          .b  = "Hello, World!",
          .c  = {std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}},
          .d  = {1, 2, 3, 4, 5},
          .e  = {{1, "one"}, {2, "two"}, {3, "three"}},
          .f  = {.cbor_tag = {}, .a = true},
          .g  = 3.14,
          .h  = "Hello, World!",
          .i  = {{"one", std::variant<int, std::map<std::string, double>, std::vector<float>>(1)},
                 {"two", std::variant<int, std::map<std::string, double>, std::vector<float>>(
                            std::map<std::string, double>{{"pi", 3.14}, {"e", 2.71}})},
                 {"three", std::variant<int, std::map<std::string, double>, std::vector<float>>(std::vector<float>{1.0f, 2.0f, 3.0f})}},
          .j  = std::nullopt};

    auto status = enc(a0);
    if (!status) {
        std::cerr << "Failed to encode A" << std::endl;
        CHECK(false);
    }

    fmt::print("Encoded A: {}\n", to_hex(data));

    // Decoding
    auto dec = make_decoder(data);
    A    a1;
    status = dec(a1);
    if (!status) {
        std::cerr << "Failed to decode A: " << status_message(status.error()) << std::endl;
        CHECK(false);
    }

    CHECK_EQ(a0.a0, a1.a0);
}