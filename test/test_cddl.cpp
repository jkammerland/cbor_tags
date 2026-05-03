#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_tags_config.h"
#include "cbor_tags/extensions/cbor_visualization.h"
#include "cbor_tags/float16_ieee754.h"

#include <array>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <fmt/format.h>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <test_util.h>
#include <tuple>
#include <variant>
#include <vector>

using namespace cbor::tags;

namespace {
template <typename T> std::string cddl_schema_inline() {
    fmt::memory_buffer buffer;
    cddl_schema_to<T>(buffer, {.row_options = {.format_by_rows = false}});
    return fmt::to_string(buffer);
}

std::size_t count_occurrences(std::string_view haystack, std::string_view needle) {
    std::size_t count = 0;
    std::size_t pos   = 0;
    while ((pos = haystack.find(needle, pos)) != std::string_view::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

struct CDDLPlainTwo {
    int         id;
    std::string name;
};

struct CDDLTaggedOne {
    static_tag<42> cbor_tag;
    std::string    value;
};

struct CDDLInlineTaggedOne {
    static constexpr std::uint64_t cbor_tag = 43;
    std::string                    value;
};

struct CDDLDynamicTaggedOne {
    dynamic_tag<std::uint64_t> cbor_tag;
    int                        value;
};

struct CDDLContainers {
    std::vector<int>                values;
    std::map<int, std::string>      names;
    std::array<int, 2>              pair;
    std::optional<CDDLPlainTwo>     maybe_plain;
    std::variant<int, CDDLPlainTwo> either_plain;
};

struct CDDLRecursiveNode {
    std::vector<CDDLRecursiveNode> children;
};

enum class CDDLUnsignedEnum : std::uint8_t {};
enum class CDDLSignedEnum : std::int8_t {};

struct CDDLEnums {
    CDDLUnsignedEnum unsigned_enum;
    CDDLSignedEnum   signed_enum;
};

struct CDDLNestedLeaf {
    int         id;
    std::string name;
};

struct CDDLNestedTagged {
    static_tag<9>  cbor_tag;
    CDDLNestedLeaf leaf;
};

struct CDDLNestedChoices {
    std::vector<std::optional<CDDLNestedLeaf>>                                values;
    std::map<std::variant<int, std::string>, std::optional<CDDLNestedTagged>> lookup;
};

namespace cddl_left {
struct Thing {
    int value;
};
} // namespace cddl_left

namespace cddl_right {
struct Thing {
    std::string value;
};
} // namespace cddl_right

struct CDDLNameCollisionRoot {
    cddl_left::Thing  left;
    cddl_right::Thing right;
};

struct CDDLEmpty {};
} // namespace

struct B129058 {
    static constexpr std::uint64_t cbor_tag = 140;
    std::vector<std::byte>         a;
    std::map<int, std::string>     b;
};

struct C122999 {
    static_tag<141>        cbor_tag;
    int                    a;
    std::string            b;
    std::optional<B129058> c;
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

struct A42121 {
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
    B129058                        j;
    C122999                        k;
};

TEST_CASE("CDDL extension") {
    fmt::memory_buffer buffer;
    cddl_schema_to<A42121>(buffer);
    CBOR_TAGS_TEST_LOG("CDDL: \n{}\n", fmt::to_string(buffer));
    CHECK(substrings_in(fmt::to_string(buffer), "uint,\n", "int / tstr,\n"));
}

TEST_CASE("CDDL aggregate tagged") {
    fmt::memory_buffer buffer;
    cddl_schema_to<A13213>(buffer, {.row_options = {.format_by_rows = true}});
    CBOR_TAGS_TEST_LOG("CDDL: \n{}\n", fmt::to_string(buffer));
}

TEST_CASE("CDDL emits RFC 8610 shapes for aggregate arrays and tag payloads") {
    CHECK_EQ(cddl_schema_inline<CDDLPlainTwo>(), "CDDLPlainTwo = [int, tstr]");
    CHECK_EQ(cddl_schema_inline<CDDLTaggedOne>(), "CDDLTaggedOne = #6.42(tstr)");
    CHECK_EQ(cddl_schema_inline<CDDLInlineTaggedOne>(), "CDDLInlineTaggedOne = #6.43(tstr)");
    CHECK_EQ(cddl_schema_inline<CDDLDynamicTaggedOne>(), "CDDLDynamicTaggedOne = #6(int)");
}

TEST_CASE("CDDL emits typed containers and registers nested definitions once") {
    const auto schema = cddl_schema_inline<CDDLContainers>();
    CBOR_TAGS_TEST_LOG("CDDL containers: \n{}\n", schema);

    CHECK(substrings_in(schema, "CDDLContainers = [[* int], {* int => tstr}, [2*2 int], CDDLPlainTwo / null, int / CDDLPlainTwo]",
                        "CDDLPlainTwo = [int, tstr]"));
    CHECK_EQ(count_occurrences(schema, "CDDLPlainTwo = [int, tstr]"), 1);
    CHECK_EQ(schema.find("array"), std::string::npos);
    CHECK_EQ(schema.find("map"), std::string::npos);
}

TEST_CASE("CDDL supports recursive aggregate containers") {
    CHECK_EQ(cddl_schema_inline<CDDLRecursiveNode>(), "CDDLRecursiveNode = [* CDDLRecursiveNode]");

    fmt::memory_buffer inline_buffer;
    cddl_schema_to<CDDLRecursiveNode>(inline_buffer, {.row_options = {.format_by_rows = false}, .always_inline = true});
    CHECK_EQ(fmt::to_string(inline_buffer), "CDDLRecursiveNode = [* CDDLRecursiveNode]");
}

TEST_CASE("CDDL gives colliding C++ short names distinct rule names") {
    const auto schema = cddl_schema_inline<CDDLNameCollisionRoot>();
    CBOR_TAGS_TEST_LOG("CDDL collision: \n{}\n", schema);

    CHECK_EQ(schema.find("CDDLNameCollisionRoot = [Thing, Thing]"), std::string::npos);
    CHECK(substrings_in(schema, "Thing = int", " = tstr"));
}

TEST_CASE("CDDL groups choices in map keys and repeated item positions") {
    CHECK_EQ(cddl_schema_inline<CDDLNestedChoices>(),
             "CDDLNestedChoices = [[* (CDDLNestedLeaf / null)], {* (int / tstr) => (CDDLNestedTagged / null)}]\n"
             "CDDLNestedTagged = #6.9(CDDLNestedLeaf)\n"
             "CDDLNestedLeaf = [int, tstr]");
}

TEST_CASE("CDDL supports root expressions for anonymous schema roots") {
    fmt::memory_buffer vector_buffer;
    cddl_schema_to<std::vector<int>>(vector_buffer, {.row_options = {.format_by_rows = false}});
    CHECK_EQ(fmt::to_string(vector_buffer), "root = [* int]");

    fmt::memory_buffer tuple_buffer;
    cddl_schema_to<std::tuple<int, std::string>>(tuple_buffer, {.row_options = {.format_by_rows = false}});
    CHECK_EQ(fmt::to_string(tuple_buffer), "root = [int, tstr]");

    fmt::memory_buffer tag_buffer;
    cddl_schema_to<static_tag<7>>(tag_buffer, {.row_options = {.format_by_rows = false}});
    CHECK_EQ(fmt::to_string(tag_buffer), "root = #6.7(any)");

    static_assert(detail::is_empty_cddl_aggregate_v<CDDLEmpty>);
}

TEST_CASE("CDDL supports catch-all header roots") {
    CHECK_EQ(cddl_schema_inline<as_array_any>(), "root = [* any]");
    CHECK_EQ(cddl_schema_inline<as_map_any>(), "root = {* any => any}");
    CHECK_EQ(cddl_schema_inline<as_tag_any>(), "root = #6(any)");
}

TEST_CASE("CDDL supports always_inline and enum underlying integer shapes") {
    fmt::memory_buffer inline_buffer;
    cddl_schema_to<CDDLContainers>(inline_buffer, {.row_options = {.format_by_rows = false}, .always_inline = true});
    const auto inline_schema = fmt::to_string(inline_buffer);
    CHECK(substrings_in(inline_schema, "[int, tstr] / null", "int / [int, tstr]"));
    CHECK_EQ(inline_schema.find("CDDLPlainTwo ="), std::string::npos);

    CHECK_EQ(cddl_schema_inline<CDDLEnums>(), "CDDLEnums = [uint, int]");
}

struct A0001 {
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
    B129058                        j;
    C122999                        k;
};

TEST_CASE("CDDL no columns") {
    fmt::memory_buffer buffer;

    cddl_schema_to<A0001>(buffer, {.row_options = {.format_by_rows = false}});
    CBOR_TAGS_TEST_LOG("CDDL: \n{}\n", fmt::to_string(buffer));

    CHECK(substrings_in(fmt::to_string(buffer), "uint,", "nint,", "int / tstr", "B129058 = #6.140([bstr, {* int => tstr}])",
                        "C122999 = #6.141([int, tstr, B129058 / null])"));
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

    CBOR_TAGS_TEST_LOG("CBOR: {}\n", to_hex(buffer));

    fmt::memory_buffer annotation;
    buffer_annotate(buffer, annotation);

    CBOR_TAGS_TEST_LOG("Annotation: \n{}\n", fmt::to_string(annotation));
}

TEST_CASE_TEMPLATE("Annotate map", T, std::vector<std::byte>, std::deque<std::byte>) {
    std::map<std::variant<int, std::string>, std::variant<int, std::string, std::map<std::string, double>>> map =
        {{1, "one"}, {"two", 2}, {3, "three"}, {"four", 4}, {"map", std::map<std::string, double>{{"pi", 3.14}, {"e", 2.71}}}};

    T buffer;

    auto enc = make_encoder(buffer);
    REQUIRE(enc(map));

    CBOR_TAGS_TEST_LOG("CBOR: {}\n", to_hex(buffer));

    fmt::memory_buffer annotation;
    buffer_annotate(buffer, annotation);

    CBOR_TAGS_TEST_LOG("Annotation: \n{}\n", fmt::to_string(annotation));
}

TEST_CASE("CWT annotation") {
    auto buffer =
        to_bytes("d28443a10126a104524173796d6d657472696345434453413235365850a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b770"
                 "37818636f61703a2f2f6c696768742e6578616d706c652e636f6d041a5612aeb0051a5610d9f0061a5610d9f007420b7158405427c1ff28d23fbad1f2"
                 "9c4c7c6a555e601d6fa29f9179bc3d7438bacaca5acd08c8d4d4f96131680c429a01f85951ecee743a52b9b63632c57209120e1c9e30");

    CBOR_TAGS_TEST_LOG("CBOR Web Token (CWT): {}\n", to_hex(buffer));

    fmt::memory_buffer annotation;
    buffer_annotate(buffer, annotation);

    CBOR_TAGS_TEST_LOG("Annotation: \n{}\n", fmt::to_string(annotation));
}

TEST_CASE("CWT payload map annotation") {
    auto buffer = to_bytes("a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b77037818636f61703a2f2f6c696768742e6578616d706c652e6"
                           "36f6d041a5612aeb0051a5610d9f0061a5610d9f007420b71");

    fmt::memory_buffer annotation;
    buffer_annotate(buffer, annotation);
    CBOR_TAGS_TEST_LOG("Annotation: \n{}\n", fmt::to_string(annotation));
}

TEST_CASE("CDDL adhoc tagging") {
    struct A {
        std::string a;
    };

    fmt::memory_buffer buffer;
    using namespace cbor::tags::literals;
    using tagA = std::pair<static_tag<140>, A>;
    cddl_schema_to<tagA>(buffer, {.row_options = {.format_by_rows = false}});
    CBOR_TAGS_TEST_LOG("CDDL: \n{}\n", fmt::to_string(buffer));

    CHECK(substrings_in(fmt::to_string(buffer), "#6.140(A)", "A = tstr"));
}

TEST_CASE("CDDL PRELUDE") {
    fmt::memory_buffer buffer;
    cddl_prelude_to(buffer);
    CBOR_TAGS_TEST_LOG("CDDL: \n{}\n", fmt::to_string(buffer));

    CHECK(substrings_in(fmt::to_string(buffer), "null = nil", "int = uint / nint"));
}

TEST_CASE_TEMPLATE("Diagnostic data 0", T, std::byte, uint8_t, char) {
    auto data = to_bytes<T>(
        "d28443a10126a104524173796d6d657472696345434453413235365850a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b770"
        "37818636f61703a2f2f6c696768742e6578616d706c652e636f6d041a5612aeb0051a5610d9f0061a5610d9f007420b7158405427c1ff28d23fbad1f2"
        "9c4c7c6a555e601d6fa29f9179bc3d7438bacaca5acd08c8d4d4f96131680c429a01f85951ecee743a52b9b63632c57209120e1c9e30");

    fmt::memory_buffer buffer;
    buffer_annotate(data, buffer);
    CBOR_TAGS_TEST_LOG("Annotation: \n{}\n", fmt::to_string(buffer));

    fmt::memory_buffer buffer1;
    buffer_diagnostic(data, buffer1, {});
    CBOR_TAGS_TEST_LOG("Diagnostic: \n{}\n", fmt::to_string(buffer1));
    fmt::format_to(std::back_inserter(buffer1), "\n --- \n");

    data = to_bytes<T>("a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b77037818636f61703a2f2f6c696768742e6578616d706c652e6"
                       "36f6d041a5612aeb0051a5610d9f0061a5610d9f007420b71");

    buffer_diagnostic(data, buffer1, {});
    CBOR_TAGS_TEST_LOG("map: \n{}\n", fmt::to_string(buffer1));

    CHECK(substrings_in(buffer1, "h'a10126'", "4: h'4173796d6d65747269634543445341323536'",
                        "h'"
                        "a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b77037818636f61703a2f2f6c696768742e6578616d706c652e636f"
                        "6d041a5612aeb0051a5610d9f0061a5610d9f007420b71'",
                        "h'"
                        "5427c1ff28d23fbad1f29c4c7c6a555e601d6fa29f9179bc3d7438bacaca5acd08c8d4d4f96131680c429a01f85951ecee743a52b9b63632c5"
                        "7209120e1c9e30'",
                        R"(3: "coap://light.example.com")", R"(7: h'0b71')", "6: 1443944944"));
}
