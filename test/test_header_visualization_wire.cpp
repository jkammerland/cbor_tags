#include "cbor_tags/extensions/cbor_visualization.h"
#include "test_util.h"

#include <deque>
#include <doctest/doctest.h>
#include <list>
#include <string>
#include <string_view>

using namespace cbor::tags;

TEST_SUITE("cbor_wire/headers_visualization") {

    TEST_CASE("reserved arguments retain the runtime error presentation boundary") {
        for (const auto *hex : {"1c", "5c", "dc", "1f", "3f", "df"}) {
            CAPTURE(std::string_view{hex});
            const auto input = to_bytes(hex);
            const auto check = [](const auto &buffer) {
                std::string diagnostic;
                CHECK_THROWS_AS(buffer_diagnostic(buffer, diagnostic), std::runtime_error);
                std::string annotation;
                CHECK_THROWS_AS(buffer_annotate(buffer, annotation), std::runtime_error);
            };
            check(input);
            check(std::deque<std::byte>{input.begin(), input.end()});
        }
    }

    TEST_CASE("string payloads are consumed once before the next item") {
        const auto input = to_bytes("62686943ff81ff07");
        const auto check = [](const auto &buffer) {
            std::string diagnostic;
            buffer_diagnostic(buffer, diagnostic, {.row_options = {.format_by_rows = false}});
            CHECK_EQ(diagnostic, "[\"hi\", h'ff81ff', 7]");

            std::string annotation;
            buffer_annotate(buffer, annotation, {.mode = AnnotationMode::no_annotation});
            CHECK_EQ(annotation, "62\n   6869\n43\n   ff81ff\n07\n");
        };
        check(input);
        check(std::deque<std::byte>{input.begin(), input.end()});
        check(std::list<std::byte>{input.begin(), input.end()});
    }

    TEST_CASE("ordinary annotation preserves actual nonminimal string headers") {
        std::string annotation;
        buffer_annotate(to_bytes("7802686959000200ff01"), annotation, {.mode = AnnotationMode::no_annotation});
        CHECK_EQ(annotation, "78 02\n   6869\n59 0002\n   00ff\n01\n");
    }

    TEST_CASE("nested indefinite containers and chunks retain their boundaries") {
        const auto  buffer = to_bytes("9f"
                                      "bf61787f61616162ffff"
                                      "5f41004202ffff"
                                      "8003ff04");
        std::string diagnostic;
        buffer_diagnostic(buffer, diagnostic, {.row_options = {.format_by_rows = false}});
        CHECK_EQ(diagnostic, "[[_ {_ \"x\": (_ \"a\", \"b\")}, (_ h'00', h'02ff'), [], 3], 4]");

        std::string annotation;
        buffer_annotate(buffer, annotation, {.mode = AnnotationMode::no_annotation});
        CHECK_EQ(annotation, "9f\n"
                             "   bf\n"
                             "      61\n"
                             "         78\n"
                             "      7f\n"
                             "         61\n"
                             "            61\n"
                             "         61\n"
                             "            62\n"
                             "         ff\n"
                             "      ff\n"
                             "   5f\n"
                             "      41\n"
                             "         00\n"
                             "      42\n"
                             "         02ff\n"
                             "      ff\n"
                             "   80\n"
                             "   03\n"
                             "   ff\n"
                             "04\n");
    }

    TEST_CASE("empty indefinite items remain distinct from definite items") {
        std::string diagnostic;
        buffer_diagnostic(to_bytes("9fffbfff7fff5fff806040a07f60ff5f40ff"), diagnostic, {.row_options = {.format_by_rows = false}});
        CHECK_EQ(diagnostic, "[[_ ], {_ }, \"\"_, ''_, [], \"\", h'', {}, (_ \"\"), (_ h'')]");
    }

    TEST_CASE("malformed headers structures and string payloads throw") {
        for (const auto hex :
             {"18",     "78",       "6268",     "4201",     "9f",       "9f01", "bf01", "bf01ff", "bf0181ffff", "7f",     "7f6161", "5f",
              "5f4101", "7f4101ff", "5f6161ff", "5f5fffff", "7f7fffff", "7c",   "ff",   "01ff",   "81ff",       "a101ff", "c0ff"}) {
            CAPTURE(hex);
            const auto  buffer = to_bytes(hex);
            std::string diagnostic;
            CHECK_THROWS_AS(buffer_diagnostic(buffer, diagnostic, {.row_options = {.format_by_rows = false}}), std::runtime_error);
            std::string annotation;
            CHECK_THROWS_WITH_AS(buffer_annotate(buffer, annotation, {.mode = AnnotationMode::no_annotation}),
                                 doctest::Contains("Malformed CBOR input"), std::runtime_error);
        }
    }

    TEST_CASE("indefinite diagnostics validate UTF-8 per chunk") {
        std::string diagnostic;
        buffer_diagnostic(to_bytes("7f61c361a9ff01"), diagnostic, {.row_options = {.format_by_rows = false}, .check_tstr_utf8 = true});
        CHECK_EQ(diagnostic, "[(_ non-utf8(1), non-utf8(1)), 1]");
    }

    TEST_CASE("indefinite traversal respects depth limits") {
        for (const auto hex : {"9f9fffff", "bfa0a0ff", "7f60ff", "5f40ff"}) {
            CAPTURE(hex);
            const auto  buffer = to_bytes(hex);
            std::string diagnostic;
            CHECK_THROWS_AS(buffer_diagnostic(buffer, diagnostic, {.row_options = {.format_by_rows = false}, .max_depth = 1}),
                            std::runtime_error);
            std::string annotation;
            CHECK_THROWS_AS(buffer_annotate(buffer, annotation, {.mode = AnnotationMode::no_annotation, .max_structure_depth = 1}),
                            std::runtime_error);
        }
    }

    TEST_CASE("empty strings do not require an additional payload depth") {
        for (const auto mode : {AnnotationMode::no_annotation, AnnotationMode::smart}) {
            for (const auto hex : {"60", "40"}) {
                CAPTURE(hex);
                std::string annotation;
                CHECK_NOTHROW(buffer_annotate(to_bytes(hex), annotation, {.mode = mode, .max_structure_depth = 1}));
                CHECK_FALSE(annotation.empty());
            }
            for (const auto hex : {"6100", "4100"}) {
                CAPTURE(hex);
                std::string annotation;
                CHECK_THROWS_AS(buffer_annotate(to_bytes(hex), annotation, {.mode = mode, .max_structure_depth = 1}), std::runtime_error);
                annotation.clear();
                CHECK_NOTHROW(buffer_annotate(to_bytes(hex), annotation, {.mode = mode, .max_structure_depth = 2}));
                CHECK_FALSE(annotation.empty());
            }
        }
    }

    TEST_CASE("diagnostic row indentation follows containers rather than tag depth") {
        const auto  input = to_bytes("c081a10102");
        std::string rows;
        buffer_diagnostic(input, rows);
        CHECK_EQ(rows, "[\n0([\n  {\n    1: 2\n  }\n])\n]");

        std::string columns;
        buffer_diagnostic(input, columns, {.row_options = {.override_array_by_columns = true}});
        CHECK_EQ(columns, "[\n0([{\n    1: 2\n  }])\n]");
    }

    TEST_CASE("presentation depth limits preserve scalar break and empty payload boundaries") {
        std::string scalar;
        CHECK_NOTHROW(buffer_diagnostic(to_bytes("01"), scalar, {.row_options = {}, .max_depth = 0}));
        CHECK_EQ(scalar, "[\n1\n]");
        std::string scalar_child;
        CHECK_NOTHROW(buffer_diagnostic(to_bytes("8101"), scalar_child, {.row_options = {.format_by_rows = false}, .max_depth = 1}));
        CHECK_EQ(scalar_child, "[[1]]");

        for (const auto hex : {"9fff", "7fff", "5fff"}) {
            CAPTURE(hex);
            std::string diagnostic;
            CHECK_NOTHROW(buffer_diagnostic(to_bytes(hex), diagnostic, {.row_options = {.format_by_rows = false}, .max_depth = 1}));
            std::string annotation;
            CHECK_THROWS_WITH_AS(
                buffer_annotate(to_bytes(hex), annotation, {.mode = AnnotationMode::no_annotation, .max_structure_depth = 1}),
                "CBOR annotation nesting depth exceeded", std::runtime_error);
        }
        for (const auto hex : {"60", "40"}) {
            CAPTURE(hex);
            std::string annotation;
            buffer_annotate(to_bytes(hex), annotation, {.mode = AnnotationMode::no_annotation, .max_structure_depth = 1});
            CHECK_EQ(annotation, std::string(hex) + "\n\n");
        }
        std::string truncated;
        CHECK_THROWS_WITH_AS(buffer_annotate(to_bytes("81"), truncated, {.mode = AnnotationMode::no_annotation, .max_structure_depth = 1}),
                             "CBOR annotation nesting depth exceeded", std::runtime_error);
        CHECK_EQ(truncated, "81\n");

        std::string offset;
        CHECK_THROWS_WITH_AS(
            buffer_diagnostic(to_bytes("8180"), offset, {.row_options = {.format_by_rows = false}, .max_depth = 2, .current_depth = 1}),
            "CBOR diagnostic nesting depth exceeded", std::runtime_error);
        CHECK_EQ(offset, "[[");
    }

    TEST_CASE("malformed diagnostics preserve their output prefix and error context") {
        struct example {
            std::string_view hex;
            std::string_view prefix;
            std::string_view error;
        };
        for (const auto &test : {
                 example{"01ff", "[1, ", "CBOR break outside indefinite item"},
                 example{"8201ff", "[[1, ", "CBOR break outside indefinite item"},
                 example{"820118", "[[1", "Malformed CBOR diagnostic array item 1"},
                 example{"a101", "[{1: ", "Malformed CBOR diagnostic map value"},
                 example{"bf01ff", "[{_ 1: ", "CBOR break outside indefinite item"},
                 example{"c0", "[0(", "Malformed CBOR diagnostic tag payload"},
                 example{"7f6161", "[(_ \"a\"", "Unterminated indefinite CBOR diagnostic string"},
                 example{"7f4100ff", "[", "Invalid indefinite CBOR diagnostic string chunk"},
                 example{"6261", "[", "Unexpected end of input"},
             }) {
            CAPTURE(test.hex);
            std::string output;
            CHECK_THROWS_WITH_AS(buffer_diagnostic(to_bytes(test.hex), output, {.row_options = {.format_by_rows = false}}),
                                 std::string(test.error).c_str(), std::runtime_error);
            CHECK_EQ(output, test.prefix);
        }
    }

    TEST_CASE("malformed ordinary annotation retains only complete header and payload rows") {
        struct example {
            std::string_view hex;
            std::string_view prefix;
            std::string_view error;
        };
        for (const auto &test : {
                 example{"01ff", "01\n", "Malformed CBOR input: break outside indefinite item"},
                 example{"816261", "81\n   62\n", "Malformed CBOR input: Unexpected end of input"},
                 example{"7f4100ff", "7f\n", "Malformed CBOR input: invalid indefinite string chunk"},
             }) {
            CAPTURE(test.hex);
            std::string output;
            CHECK_THROWS_WITH_AS(buffer_annotate(to_bytes(test.hex), output, {.mode = AnnotationMode::no_annotation}),
                                 std::string(test.error).c_str(), std::runtime_error);
            CHECK_EQ(output, test.prefix);
        }
    }

    TEST_CASE("ordinary visualization retains permissive reserved simple decoding") {
        const auto  input = to_bytes("f818f81f");
        std::string diagnostic;
        buffer_diagnostic(input, diagnostic, {.row_options = {.format_by_rows = false}});
        CHECK_EQ(diagnostic, "[simple(24), simple(31)]");
        std::string annotation;
        buffer_annotate(input, annotation, {.mode = AnnotationMode::no_annotation});
        CHECK_EQ(annotation, "f8 18\nf8 1f\n");
    }

    TEST_CASE("already decoded diagnostic headers leave the following item unread") {
        const auto input = to_bytes("c09f7f60ffa10102ff03");
        const auto check = [](const auto &buffer) {
            auto       dec = make_decoder(buffer);
            as_tag_any header;
            REQUIRE(dec(header));
            std::string output;
            make_diagnostic_visitor(output, dec, {.row_options = {.format_by_rows = false}})(header);
            CHECK_EQ(output, "0([_ (_ \"\"), {1: 2}])");
            int following{};
            REQUIRE(dec(following));
            CHECK_EQ(following, 3);
        };
        check(input);
        check(std::deque<std::byte>{input.begin(), input.end()});
        check(std::list<std::byte>{input.begin(), input.end()});
    }

} // TEST_SUITE
