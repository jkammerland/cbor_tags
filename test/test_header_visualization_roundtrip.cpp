#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/cbor_visualization.h"

#include <deque>
#include <doctest/doctest.h>
#include <map>
#include <string>
#include <vector>

using namespace cbor::tags;

TEST_SUITE("roundtrip/headers_visualization") {

    TEST_CASE("generated definite values retain their diagnostic rendering") {
        const std::map<int, std::string> input{{1, "one"}, {2, "two"}};
        std::vector<std::byte>           buffer;
        auto                             enc = make_encoder(buffer);
        REQUIRE(enc(input, std::string{"tail"}));

        auto                       dec = make_decoder(buffer);
        std::map<int, std::string> decoded;
        std::string                tail;
        REQUIRE(dec(decoded, tail));
        CHECK_EQ(decoded, input);
        CHECK_EQ(tail, "tail");

        const auto check = [](const auto &encoded) {
            std::string diagnostic;
            buffer_diagnostic(encoded, diagnostic, {.row_options = {.format_by_rows = false}});
            CHECK_EQ(diagnostic, "[{1: \"one\", 2: \"two\"}, \"tail\"]");
            std::string annotation;
            CHECK_NOTHROW(buffer_annotate(encoded, annotation, {.mode = AnnotationMode::no_annotation}));
            CHECK_FALSE(annotation.empty());
        };
        check(buffer);
        check(std::deque<std::byte>{buffer.begin(), buffer.end()});
    }

    TEST_CASE("generated indefinite values leave subsequent values readable") {
        const std::map<int, std::string> input{{1, "one"}, {2, "two"}};
        const std::vector<int>           numbers{3, 4};
        const std::string                label{"tail"};
        std::vector<std::byte>           buffer;
        auto                             enc = make_encoder(buffer);
        REQUIRE(enc(as_indefinite{input}, as_indefinite{numbers}, as_indefinite{label}, 5));

        auto                       dec = make_decoder(buffer);
        std::map<int, std::string> decoded;
        std::vector<int>           decoded_numbers;
        std::string                decoded_label;
        int                        sentinel{};
        REQUIRE(dec(decoded, decoded_numbers, decoded_label, sentinel));
        CHECK_EQ(decoded, input);
        CHECK_EQ(decoded_numbers, numbers);
        CHECK_EQ(decoded_label, label);
        CHECK_EQ(sentinel, 5);

        const auto check = [](const auto &encoded) {
            std::string diagnostic;
            buffer_diagnostic(encoded, diagnostic, {.row_options = {.format_by_rows = false}});
            CHECK_EQ(diagnostic, "[{_ 1: \"one\", 2: \"two\"}, [_ 3, 4], (_ \"tail\"), 5]");
            std::string annotation;
            CHECK_NOTHROW(buffer_annotate(encoded, annotation, {.mode = AnnotationMode::no_annotation}));
            CHECK_FALSE(annotation.empty());
        };
        check(buffer);
        check(std::deque<std::byte>{buffer.begin(), buffer.end()});
    }

} // TEST_SUITE
