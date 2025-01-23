#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/cbor_cddl.h"
#include "cbor_tags/float16_ieee754.h"

#include <deque>
#include <doctest/doctest.h>
#include <fmt/format.h>
#include <string>
#include <test_util.h>
#include <variant>
#include <vector>

using namespace cbor::tags;

TEST_CASE("CDDL extension") {
    fmt::memory_buffer     buffer;
    std::vector<std::byte> cbor_buffer = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}};
    CDDL(cbor_buffer, buffer);
    fmt::print("CDDL: {}\n", fmt::to_string(buffer));
}

struct A1212 {
    // static constexpr std::uint64_t cbor_tag = 139;
    int                        pos;
    int                        nega;
    std::string                c;
    std::vector<std::byte>     d;
    std::map<int, std::string> e;
    struct B1212 {
        static_tag<140> cbor_tag;
        struct B1313 {
            static_tag<142> cbor_tag;

            int         a;
            std::string b;
        } b1313;
        int a;
    } f;
    float16_t      g;
    float          h;
    double         i;
    bool           j;
    std::nullptr_t k;
};

TEST_CASE_TEMPLATE("Annotate", T, std::vector<std::byte>, std::deque<std::byte>) {
    A1212 a1212{.pos  = 1,
                .nega = -1,
                .c    = "Hello world!",
                .d    = {std::byte{'a'}, std::byte{'b'}, std::byte{'c'}, std::byte{'d'}},
                .e    = {{1, "one"}, {2, "two"}, {3, "three"}},
                .f    = {.cbor_tag = {}, .b1313 = {.cbor_tag = {}, .a = 1000000, .b = "aaaaaaaaaaaaaaaaaaaaaaaa"}, .a = 42},
                .g    = float16_t{3.14},
                .h    = 3.14f,
                .i    = 3.14,
                .j    = true,
                .k    = nullptr};

    T buffer;

    auto enc = make_encoder(buffer);
    REQUIRE(enc(a1212));

    fmt::print("CBOR: {}\n", to_hex(buffer));

    fmt::memory_buffer annotation;
    annotate(buffer, annotation);

    fmt::print("Annotation: \n{}\n", fmt::to_string(annotation));
}

TEST_CASE_TEMPLATE("Annotate map", T, std::vector<std::byte>, std::deque<std::byte>) {
    std::map<std::variant<int, std::string>, std::variant<int, std::string, std::map<std::string, double>>> map =
        {{1, "one"}, {"two", 2}, {3, "three"}, {"four", 4}, {"map", std::map<std::string, double>{{"pi", 3.14}, {"e", 2.71}}}};

    T buffer;

    auto enc = make_encoder(buffer);
    REQUIRE(enc(map));

    fmt::print("CBOR: {}\n", to_hex(buffer));

    fmt::memory_buffer annotation;
    annotate(buffer, annotation);

    fmt::print("Annotation: \n{}\n", fmt::to_string(annotation));
}

TEST_CASE("CWT annotation") {
    auto buffer =
        to_bytes("d28443a10126a104524173796d6d657472696345434453413235365850a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b770"
                 "37818636f61703a2f2f6c696768742e6578616d706c652e636f6d041a5612aeb0051a5610d9f0061a5610d9f007420b7158405427c1ff28d23fbad1f2"
                 "9c4c7c6a555e601d6fa29f9179bc3d7438bacaca5acd08c8d4d4f96131680c429a01f85951ecee743a52b9b63632c57209120e1c9e30");

    fmt::memory_buffer annotation;
    annotate(buffer, annotation);

    fmt::print("Annotation: \n{}\n", fmt::to_string(annotation));
}

TEST_CASE("CWT payload map annotation") {
    auto buffer = to_bytes("a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b77037818636f61703a2f2f6c696768742e6578616d706c652e6"
                           "36f6d041a5612aeb0051a5610d9f0061a5610d9f007420b71");

    fmt::memory_buffer annotation;
    annotate(buffer, annotation);
    fmt::print("Annotation: \n{}\n", fmt::to_string(annotation));
}