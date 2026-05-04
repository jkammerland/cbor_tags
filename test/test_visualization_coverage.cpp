#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/cbor_visualization.h"
#include "test_util.h"

#include <cstddef>
#include <doctest/doctest.h>
#include <functional>
#include <map>
#include <nameof.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using namespace cbor::tags;
using namespace std::string_literals;
using namespace std::string_view_literals;

namespace {
struct VisualizationInner {
    int         id{};
    std::string name;
};

struct VisualizationTagged {
    static_tag<777> cbor_tag;
    int             id{};
    std::string     name;
};
} // namespace

TEST_CASE("visualization names cover primitive, optional, variant, and tag types") {
    CHECK_EQ(getName<std::uint64_t>(), "uint"sv);
    CHECK_EQ(getName<negative>(), "nint"sv);
    CHECK_EQ(getName<int>(), "int"sv);
    CHECK_EQ(getName<std::string>(), "tstr"sv);
    CHECK_EQ(getName<std::vector<std::byte>>(), "bstr"sv);
    CHECK_EQ(getName<std::vector<int>>(), "array"sv);
    CHECK_EQ(getName<std::map<int, int>>(), "map"sv);
    CHECK_EQ(getName<float16_t>(), "float16"sv);
    CHECK_EQ(getName<float>(), "float32"sv);
    CHECK_EQ(getName<double>(), "float64"sv);
    CHECK_EQ(getName<bool>(), "bool"sv);
    CHECK_EQ(getName<std::nullptr_t>(), "null"sv);
    CHECK_EQ(getName<std::optional<int>>(), "int / null"s);
    CHECK_EQ(getTagDef(static_tag<777>{}), "#6.777"s);
}

TEST_CASE("cddl helpers generate prelude and schemas") {
    std::string prelude;
    cddl_prelude_to(prelude);
    CHECK(prelude.find("any = #") != std::string::npos);
    CHECK(prelude.find("float64 = #7.27") != std::string::npos);

    std::string row_schema;
    cddl_schema_to<VisualizationTagged>(row_schema);
    CHECK(row_schema.find("VisualizationTagged = #6.777") != std::string::npos);
    CHECK(row_schema.find("int") != std::string::npos);
    CHECK(row_schema.find("tstr") != std::string::npos);

    std::string inline_schema;
    cddl_schema_to<VisualizationInner>(inline_schema, {.row_options = {.format_by_rows = false}});
    CHECK(inline_schema.find("VisualizationInner = [int, tstr]") != std::string::npos);
}

TEST_CASE("cddl helpers cover tuple and tagged tuple schemas") {
    std::string tuple_schema;
    cddl_schema_to<std::tuple<int, std::string>>(tuple_schema, {.row_options = {.format_by_rows = false}});
    CHECK_EQ(tuple_schema, "root = [int, tstr]");

    std::string tagged_tuple_schema;
    cddl_schema_to<std::tuple<static_tag<7>, int, std::string>>(tagged_tuple_schema, {.row_options = {.format_by_rows = false}});
    CHECK_EQ(tagged_tuple_schema, "root = #6.7([int, tstr])");
}

TEST_CASE("tagged tuple schema matches encoded tuple shape") {
    using tagged_tuple = std::tuple<static_tag<7>, int, std::string>;

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(tagged_tuple{static_tag<7>{}, 1, "x"}));

    CHECK_EQ(buffer, std::vector<std::byte>{std::byte{0xC7}, std::byte{0x82}, std::byte{0x01}, std::byte{0x61}, std::byte{0x78}});
}

TEST_CASE("cddl context handles duplicate registration, copy, and clear") {
    detail::CDDLContext context;
    context.register_type<VisualizationInner>({}, std::ref(context));
    context.register_type<VisualizationInner>({}, std::ref(context));
    context.register_type<int>({}, std::ref(context));

    CHECK(context.contains(nameof::nameof_type<VisualizationInner>()));
    CHECK_EQ(context.definitions.size(), 1);

    detail::CDDLContext copied{context};
    CHECK(copied.contains(nameof::nameof_type<VisualizationInner>()));

    context.clear();
    CHECK(context.definitions.empty());

    context.register_type<VisualizationTagged>({}, std::ref(context));
    CHECK(context.contains(nameof::nameof_type<VisualizationTagged>()));
    CHECK_EQ(context.definitions.size(), 1);
}

TEST_CASE("buffer annotation handles empty input, text chunks, arrays, maps, and tags") {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(wrap_as_array{std::string{"hi"}, std::vector<int>{1, 2}, std::map<int, int>{{3, 4}}, make_tag_pair(static_tag<42>{}, 7)}));

    std::string annotation;
    buffer_annotate(buffer, annotation, {.max_depth = 2, .mode = AnnotationMode::no_annotation});
    INFO(annotation);
    CHECK(annotation.find("82") != std::string::npos);
    CHECK(annotation.find("68") != std::string::npos);
    CHECK(annotation.find("69") != std::string::npos);
    CHECK(annotation.find("d8") != std::string::npos);
    CHECK(annotation.find("2a") != std::string::npos);

    std::string            empty_annotation{"unchanged"};
    std::vector<std::byte> empty;
    buffer_annotate(empty, empty_annotation, {.mode = AnnotationMode::no_annotation});
    CHECK_EQ(empty_annotation, "unchanged");

    CHECK_THROWS_AS(buffer_annotate(buffer, annotation, {.diagnostic_data = true}), std::runtime_error);
}

TEST_CASE("buffer annotation defaults to cbor diag style indefinite map") {
    const auto buffer = to_bytes("bf6346756ef563416d7421ff");

    std::string annotation;
    buffer_annotate(buffer, annotation, {.annotation_column = 13});

    CHECK_EQ(annotation, "bf           # map(*)\n"
                         "   63        #   text(3)\n"
                         "      46756e #     \"Fun\"\n"
                         "   f5        #   true, simple(21)\n"
                         "   63        #   text(3)\n"
                         "      416d74 #     \"Amt\"\n"
                         "   21        #   negative(-2)\n"
                         "   ff        #   break\n");
}

TEST_CASE("buffer annotation supports no annotation mode and common smart CBOR values") {
    const auto buffer = to_bytes("8201c06378797a");

    std::string no_annotation;
    buffer_annotate(buffer, no_annotation, {.mode = AnnotationMode::no_annotation});
    CHECK(no_annotation.find('#') == std::string::npos);

    std::string annotation;
    buffer_annotate(std::deque<std::byte>{buffer.begin(), buffer.end()}, annotation, {.annotation_column = 24});

    INFO(annotation);
    CHECK(annotation.find("# array(2)") != std::string::npos);
    CHECK(annotation.find("#   unsigned(1)") != std::string::npos);
    CHECK(annotation.find("#   tdate, tag(0)") != std::string::npos);
    CHECK(annotation.find("#     text(3)") != std::string::npos);
    CHECK(annotation.find("#       \"xyz\"") != std::string::npos);
}

TEST_CASE("smart buffer annotation handles floats, null, undefined, and sequences") {
    const auto buffer = to_bytes("f93c00fa3f800000fb4000000000000000f6f70102");

    std::string annotation;
    buffer_annotate(buffer, annotation, {.annotation_column = 40});

    INFO(annotation);
    CHECK(annotation.find("float16(1)") != std::string::npos);
    CHECK(annotation.find("float32(1)") != std::string::npos);
    CHECK(annotation.find("float64(2)") != std::string::npos);
    CHECK(annotation.find("null, simple(22)") != std::string::npos);
    CHECK(annotation.find("undefined, simple(23)") != std::string::npos);
    CHECK(annotation.find("unsigned(1)") != std::string::npos);
    CHECK(annotation.find("unsigned(2)") != std::string::npos);
}

TEST_CASE("smart buffer annotation wraps string payloads before the annotation column") {
    const auto buffer = to_bytes("6c48656c6c6f20776f726c6421");

    std::string annotation;
    buffer_annotate(buffer, annotation, {.max_depth = 3, .annotation_column = 16});

    INFO(annotation);
    CHECK(annotation.find("6c              # text(12)") != std::string::npos);
    CHECK(annotation.find("   48656c       #   \"Hello world!\"") != std::string::npos);
    CHECK(annotation.find("   6c6f20") != std::string::npos);
    CHECK(annotation.find("   776f72") != std::string::npos);
    CHECK(annotation.find("   6c6421") != std::string::npos);
}

TEST_CASE("smart buffer annotation handles indefinite arrays and byte strings") {
    const auto buffer = to_bytes("9f5f42010241ffffff");

    std::string annotation;
    buffer_annotate(buffer, annotation, {.annotation_column = 24});

    INFO(annotation);
    CHECK(annotation.find("# array(*)") != std::string::npos);
    CHECK(annotation.find("#   bytes(*)") != std::string::npos);
    CHECK(annotation.find("#     bytes(2)") != std::string::npos);
    CHECK(annotation.find("#       h'0102'") != std::string::npos);
    CHECK(annotation.find("#     bytes(1)") != std::string::npos);
    CHECK(annotation.find("#       h'ff'") != std::string::npos);
    CHECK(annotation.find("#     break") != std::string::npos);
    CHECK(annotation.find("#   break") != std::string::npos);
}

TEST_CASE("smart buffer annotation reports malformed input instead of truncating") {
    constexpr AnnotationOptions options{.annotation_column = 13};

    std::string annotation{"unchanged"};
    CHECK_THROWS_AS(buffer_annotate(to_bytes("18"), annotation, options), std::runtime_error);
    CHECK_EQ(annotation, "unchanged");
    CHECK_THROWS_AS(buffer_annotate(to_bytes("430102"), annotation, options), std::runtime_error);
    CHECK_THROWS_AS(buffer_annotate(to_bytes("1c"), annotation, options), std::runtime_error);
    CHECK_THROWS_AS(buffer_annotate(to_bytes("ff"), annotation, options), std::runtime_error);
    CHECK_THROWS_AS(buffer_annotate(to_bytes("bf01ff"), annotation, options), std::runtime_error);
    CHECK_THROWS_AS(buffer_annotate(to_bytes("8180"), annotation, {.annotation_column = 13, .max_structure_depth = 1}), std::runtime_error);
    CHECK_THROWS_AS(buffer_annotate(to_bytes("01"), annotation, {.annotation_column = 2}), std::runtime_error);
}

TEST_CASE("smart buffer annotation renders uint64 max negative exactly") {
    const auto buffer = to_bytes("3bffffffffffffffff");

    std::string annotation;
    buffer_annotate(buffer, annotation, {.annotation_column = 40});

    CHECK(annotation.find("negative(-18446744073709551616)") != std::string::npos);
}

TEST_CASE("smart buffer annotation stress tests adversarial single byte inputs") {
    for (std::uint16_t value = 0; value <= std::numeric_limits<std::uint8_t>::max(); ++value) {
        std::vector<std::byte> input{static_cast<std::byte>(value)};
        std::string            annotation{"sentinel"};

        try {
            buffer_annotate(input, annotation, {.annotation_column = 80});
            CHECK_NE(annotation, "sentinel");
            CHECK(annotation.find('#') != std::string::npos);
        } catch (const std::runtime_error &) { CHECK_EQ(annotation, "sentinel"); }
    }
}

TEST_CASE("smart buffer annotation stress tests targeted malformed inputs") {
    constexpr std::string_view malformed[] = {
        "18",                 // missing uint8 argument
        "1c",                 // invalid additional information
        "430102",             // truncated byte string
        "5f60ff",             // byte string with text chunk
        "7f40ff",             // text string with byte chunk
        "9f",                 // unterminated array
        "bf01ff",             // indefinite map missing value
        "c0",                 // tag missing payload
        "ff",                 // stray break
        "818181818181818100", // excessive nesting with low max_structure_depth
    };

    for (const auto hex : malformed) {
        std::string annotation{"sentinel"};
        CHECK_THROWS_AS(buffer_annotate(to_bytes(hex), annotation, {.annotation_column = 80, .max_structure_depth = 4}),
                        std::runtime_error);
        CHECK_EQ(annotation, "sentinel");
    }
}

TEST_CASE("buffer diagnostic renders arrays, maps, tags, strings, floats, bools, null, and simple values") {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(
        enc(wrap_as_array{std::map<int, std::string>{{1, "one"}}, make_tag_pair(static_tag<42>{}, std::vector<std::byte>{std::byte{0xAB}}),
                          float16_t{static_cast<std::uint16_t>(0x3C00)}, float{1.0F}, double{2.0}, true, nullptr, simple{16}}));

    std::string diagnostic;
    buffer_diagnostic(buffer, diagnostic, {.row_options = {.format_by_rows = false}});
    INFO(diagnostic);
    CHECK(diagnostic.find("1: \"one\"") != std::string::npos);
    CHECK(diagnostic.find("42(h'ab')") != std::string::npos);
    CHECK(diagnostic.find("true") != std::string::npos);
    CHECK(diagnostic.find("null") != std::string::npos);
    CHECK(diagnostic.find("simple(16)") != std::string::npos);

    struct DummyDecoder {};
    DummyDecoder dummy;
    std::string  simple_diagnostic;
    make_diagnostic_visitor(simple_diagnostic, dummy, {})(simple{16});
    CHECK_EQ(simple_diagnostic, "simple(16)");
}

TEST_CASE("buffer diagnostic rejects requested utf8 validation") {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(std::string{"hi"}));

    DiagnosticOptions options;
    options.check_tstr_utf8 = true;

    std::string diagnostic;
    CHECK_THROWS_AS(buffer_diagnostic(buffer, diagnostic, options), std::runtime_error);
}
