#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/cbor_cddl.h"
#include "cbor_tags/float16_ieee754.h"

#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <fmt/format.h>
#include <map>
#include <optional>
#include <string>
#include <sys/types.h>
#include <test_util.h>
#include <variant>
#include <vector>

using namespace cbor::tags;

struct B {
    static constexpr std::uint64_t cbor_tag = 140;
    std::vector<std::byte>         a;
    std::map<int, std::string>     b;
};

struct C {
    static_tag<141>  cbor_tag;
    int              a;
    std::string      b;
    std::optional<B> c;
};

struct A13213 {
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

TEST_CASE("CDDL extension") {
    fmt::memory_buffer buffer;

    struct A {
        uint32_t                       a1;
        int                            a;
        double                         b;
        float                          c;
        bool                           d;
        std::string                    e;
        std::vector<std::byte>         f;
        std::map<int, std::string>     g;
        std::variant<int, std::string> h;
        std::optional<int>             i;
        B                              j;
        C                              k;
    };

    cddl_to<A>(buffer);
    fmt::print("CDDL: \n{}\n", fmt::to_string(buffer));
}

TEST_CASE("CDDL aggregate tagged") {
    fmt::memory_buffer buffer;
    cddl_to(buffer, A13213{}, {.row_options = {.format_by_rows = true}});
    fmt::print("CDDL: \n{}\n", fmt::to_string(buffer));
}

TEST_CASE("CDDL no columns") {
    fmt::memory_buffer buffer;
    struct A {
        uint32_t                       a1;
        negative                       aminus;
        int                            a;
        double                         b;
        float                          c;
        bool                           d;
        std::string                    e;
        std::vector<std::byte>         f;
        std::map<int, std::string>     g;
        std::variant<int, std::string> h;
        std::optional<int>             i;
        B                              j;
        C                              k;
    };

    cddl_to<A>(buffer, {.row_options = {.format_by_rows = false}});
    fmt::print("CDDL: \n{}\n", fmt::to_string(buffer));

    CHECK(substrings_in(fmt::to_string(buffer), "uint,", "nint,", "int / tstr", "B = #6.140([bstr, map])",
                        "C = #6.141([int, tstr, B / null])"));
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
    buffer_annotate(buffer, annotation);

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
    buffer_annotate(buffer, annotation);

    fmt::print("Annotation: \n{}\n", fmt::to_string(annotation));
}

TEST_CASE("CWT annotation") {
    auto buffer =
        to_bytes("d28443a10126a104524173796d6d657472696345434453413235365850a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b770"
                 "37818636f61703a2f2f6c696768742e6578616d706c652e636f6d041a5612aeb0051a5610d9f0061a5610d9f007420b7158405427c1ff28d23fbad1f2"
                 "9c4c7c6a555e601d6fa29f9179bc3d7438bacaca5acd08c8d4d4f96131680c429a01f85951ecee743a52b9b63632c57209120e1c9e30");

    fmt::print("CBOR Web Token (CWT): {}\n", to_hex(buffer));

    fmt::memory_buffer annotation;
    buffer_annotate(buffer, annotation);

    fmt::print("Annotation: \n{}\n", fmt::to_string(annotation));
}

TEST_CASE("CWT payload map annotation") {
    auto buffer = to_bytes("a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b77037818636f61703a2f2f6c696768742e6578616d706c652e6"
                           "36f6d041a5612aeb0051a5610d9f0061a5610d9f007420b71");

    fmt::memory_buffer annotation;
    buffer_annotate(buffer, annotation);
    fmt::print("Annotation: \n{}\n", fmt::to_string(annotation));
}

TEST_CASE("CDDL adhoc tagging") {
    struct A {
        std::string a;
    };

    fmt::memory_buffer buffer;
    using namespace cbor::tags::literals;
    cddl_to(buffer, make_tag_pair(140_tag, A{"Hello world!"}), {.row_options = {.format_by_rows = false}});
    fmt::print("CDDL: \n{}\n", fmt::to_string(buffer));

    CHECK(substrings_in(fmt::to_string(buffer), "#6.140(A)", "A = tstr"));
}

TEST_CASE("CDDL PRELUDE") {
    fmt::memory_buffer buffer;
    cddl_prelude_to(buffer);
    fmt::print("CDDL: \n{}\n", fmt::to_string(buffer));

    CHECK(substrings_in(fmt::to_string(buffer), "null = nil", "int = uint / nint"));
}

TEST_CASE("Diagnostic data 0") {
    std::vector<std::byte> data =
        to_bytes("d28443a10126a104524173796d6d657472696345434453413235365850a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b770"
                 "37818636f61703a2f2f6c696768742e6578616d706c652e636f6d041a5612aeb0051a5610d9f0061a5610d9f007420b7158405427c1ff28d23fbad1f2"
                 "9c4c7c6a555e601d6fa29f9179bc3d7438bacaca5acd08c8d4d4f96131680c429a01f85951ecee743a52b9b63632c57209120e1c9e30");

    fmt::memory_buffer buffer;
    buffer_annotate(data, buffer);
    fmt::print("Annotation: \n{}\n", fmt::to_string(buffer));

    fmt::memory_buffer buffer1;
    buffer_diagnostic(data, buffer1, {});
    fmt::print("Diagnostic: \n{}\n", fmt::to_string(buffer1));
    fmt::format_to(std::back_inserter(buffer1), "\n --- \n");

    data = to_bytes("a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b77037818636f61703a2f2f6c696768742e6578616d706c652e6"
                    "36f6d041a5612aeb0051a5610d9f0061a5610d9f007420b71");

    buffer_diagnostic(data, buffer1, {});
    fmt::print("map: \n{}\n", fmt::to_string(buffer1));

    CHECK(substrings_in(buffer1, "h'a10126'", "4: h'4173796d6d65747269634543445341323536'",
                        "h'"
                        "a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b77037818636f61703a2f2f6c696768742e6578616d706c652e636f"
                        "6d041a5612aeb0051a5610d9f0061a5610d9f007420b71'",
                        "h'"
                        "5427c1ff28d23fbad1f29c4c7c6a555e601d6fa29f9179bc3d7438bacaca5acd08c8d4d4f96131680c429a01f85951ecee743a52b9b63632c5"
                        "7209120e1c9e30'",
                        R"(3: "coap://light.example.com")", R"(7: h'0b71')", "6: 1443944944"));
}