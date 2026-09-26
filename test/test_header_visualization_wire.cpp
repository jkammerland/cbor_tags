#include "cbor_tags/extensions/cbor_visualization.h"
#include "test_util.h"

#include <deque>
#include <doctest/doctest.h>
#include <list>
#include <string>
#include <string_view>

using namespace cbor::tags;

TEST_SUITE("cbor_wire/headers_visualization") {

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

} // TEST_SUITE
