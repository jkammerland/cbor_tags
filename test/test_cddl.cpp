#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/cbor_cddl.h"
#include "cbor_tags/float16_ieee754.h"

#include <deque>
#include <doctest/doctest.h>
#include <fmt/format.h>
#include <string>
#include <test_util.h>
#include <vector>

using namespace cbor::tags;

TEST_CASE("CDDL extension") {
    fmt::memory_buffer     buffer;
    std::vector<std::byte> cbor_buffer = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}};
    CDDL(cbor_buffer, buffer);
    fmt::print("CDDL: {}\n", fmt::to_string(buffer));
}

struct A1212 {
    static constexpr std::uint64_t cbor_tag = 139;
    int                            pos;
    int                            nega;
    std::string                    c;
    std::vector<std::byte>         d;
    std::map<int, std::string>     e;
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

TEST_CASE("Annotate") {
    A1212 a1212{.pos  = 1,
                .nega = -1,
                .c    = "Hello world!",
                .d    = {std::byte{'a'}, std::byte{'b'}, std::byte{'c'}, std::byte{'d'}},
                .e    = {{1, "one"}, {2, "two"}, {3, "three"}},
                .f    = {.cbor_tag = {}, .b1313 = {.cbor_tag = {}, .a = 1000000, .b = "aaaaaaaaaaaaaaaaaaaaaaaa"}, .a = 42},
                .g    = float16_t{3.14f},
                .h    = 3.14f,
                .i    = 3.14,
                .j    = true,
                .k    = nullptr};

    std::vector<std::byte> buffer;

    auto enc = make_encoder(buffer);
    REQUIRE(enc(a1212));

    fmt::print("CBOR: {}\n", to_hex(buffer));

    fmt::memory_buffer annotation;
    annotate(buffer, annotation);

    fmt::print("Annotation: \n{}\n", fmt::to_string(annotation));
}