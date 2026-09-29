#include "fuzz_support.h"
#include "wire_domains.h"

#include <cbor_tags/extensions/cbor_visualization.h>
#include <stdexcept>

namespace cbor_fuzz {
struct rendered {
    std::string text;
    std::string error;
    bool        operator==(const rendered &) const = default;
};

template <typename Buffer> rendered render(const Buffer &input, bool annotate) {
    rendered result;
    try {
        if (annotate) {
            buffer_annotate(input, result.text, {.max_structure_depth = 32, .max_input_size = 4096, .max_output_size = 262144});
        } else {
            buffer_diagnostic(input, result.text, {.max_depth = 32});
        }
    } catch (const std::runtime_error &error) {
        // Malformed wire data has a documented throwing API. Compare the complete
        // failure (including partial output) across buffers instead of ignoring it.
        result.error = error.what();
    }
    return result;
}

void visualization_wire(const bytes &input) {
    const std::deque<std::uint8_t> segmented(input.begin(), input.end());
    for (const bool annotate : {false, true}) {
        const auto contiguous = render(input, annotate);
        EXPECT_EQ(contiguous, render(segmented, annotate));
        EXPECT_LE(contiguous.text.size(), 262144U);
    }
}
FUZZ_TEST(CborVisualizationWire, visualization_wire).WithDomains(wire_domain()).WithSeeds(wire_seeds);

void valid_record_visualization(const record &input) {
    bytes buffer;
    auto  enc = make_encoder(buffer);
    ASSERT_TRUE(enc(input));

    for (const bool annotate : {false, true}) {
        const auto result = render(buffer, annotate);
        EXPECT_TRUE(result.error.empty()) << result.error;
        EXPECT_FALSE(result.text.empty());
    }
}
FUZZ_TEST(CborVisualization, valid_record_visualization).WithDomains(record_domain());

struct schema_record {
    record                                           nested;
    max_size<std::string, 128>                       label;
    std::map<std::string, std::vector<std::int64_t>> history;
    std::variant<std::int64_t, std::string, bool>    choice;
    std::array<double, 3>                            coordinates;
};

void cddl_options(const std::string &root_name, bool rows, bool inline_types, std::uint8_t indent) {
    const CDDLOptions options{.row_options   = {.format_by_rows = rows, .offset = indent},
                              .always_inline = inline_types,
                              .root_name     = root_name};
    std::string       text;
    cddl_schema_to<schema_record>(text, options);
    std::vector<char> chars;
    cddl_schema_to<schema_record>(chars, options);

    EXPECT_TRUE(std::ranges::equal(text, chars));
    EXPECT_TRUE(text.starts_with(root_name + " = "));
    EXPECT_NE(text.find("#6.100"), std::string::npos);
    EXPECT_NE(text.find("tstr"), std::string::npos);
}
FUZZ_TEST(CborVisualization, cddl_options)
    .WithDomains(fuzztest::InRegexp("[a-z][a-z0-9]{0,23}"), fuzztest::Arbitrary<bool>(), fuzztest::Arbitrary<bool>(),
                 fuzztest::InRange<std::uint8_t>(0, 8));
} // namespace cbor_fuzz
