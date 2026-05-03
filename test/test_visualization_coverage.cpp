#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/cbor_visualization.h"
#include "test_util.h"

#include <cstddef>
#include <doctest/doctest.h>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
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
    CHECK(inline_schema.find("VisualizationInner = (int, tstr)") != std::string::npos);
}

TEST_CASE("buffer annotation handles empty input, text chunks, arrays, maps, and tags") {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(wrap_as_array{std::string{"hi"}, std::vector<int>{1, 2}, std::map<int, int>{{3, 4}}, make_tag_pair(static_tag<42>{}, 7)}));

    std::string annotation;
    buffer_annotate(buffer, annotation, {.max_depth = 2});
    INFO(annotation);
    CHECK(annotation.find("82") != std::string::npos);
    CHECK(annotation.find("68") != std::string::npos);
    CHECK(annotation.find("69") != std::string::npos);
    CHECK(annotation.find("d8") != std::string::npos);
    CHECK(annotation.find("2a") != std::string::npos);

    std::string empty_annotation{"unchanged"};
    std::vector<std::byte> empty;
    buffer_annotate(empty, empty_annotation);
    CHECK_EQ(empty_annotation, "unchanged");

    CHECK_THROWS_AS(buffer_annotate(buffer, annotation, {.diagnostic_data = true}), std::runtime_error);
}

TEST_CASE("buffer diagnostic renders arrays, maps, tags, strings, floats, bools, null, and simple values") {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    REQUIRE(enc(wrap_as_array{std::map<int, std::string>{{1, "one"}}, make_tag_pair(static_tag<42>{}, std::vector<std::byte>{std::byte{0xAB}}),
                              float16_t{static_cast<std::uint16_t>(0x3C00)}, float{1.0F}, double{2.0}, true, nullptr, simple{16}}));

    std::string diagnostic;
    buffer_diagnostic(buffer, diagnostic, {.row_options = {.format_by_rows = false}});
    INFO(diagnostic);
    CHECK(diagnostic.find("1: \"one\"") != std::string::npos);
    CHECK(diagnostic.find("42(h'ab')") != std::string::npos);
    CHECK(diagnostic.find("true") != std::string::npos);
    CHECK(diagnostic.find("null") != std::string::npos);

    struct DummyDecoder {};
    DummyDecoder dummy;
    std::string  simple_diagnostic;
    make_diagnostic_visitor(simple_diagnostic, dummy, {})(simple{16});
    CHECK_EQ(simple_diagnostic, "simple");
}
