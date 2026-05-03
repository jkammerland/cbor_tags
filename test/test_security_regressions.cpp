#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/cbor_visualization.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace cbor::tags;

namespace {
struct SecurityTaggedDirect {
    static_tag<12> cbor_tag;
    int            value{};

  private:
    friend cbor::tags::Access;

    template <typename Encoder> constexpr auto encode(Encoder &enc) const { return enc(value); }
    template <typename Decoder> constexpr auto decode(Decoder &dec) { return dec(value); }
};

struct SecurityRootItem {
    int value{};
};

struct SecurityRecursiveNode {
    std::vector<SecurityRecursiveNode> children;
};

std::vector<std::byte> uint64_max_length_text_with_one_payload_byte() {
    return {std::byte{0x7B}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
            std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{'x'}};
}

std::vector<std::byte> uint64_max_length_bstr_with_one_payload_byte() {
    return {std::byte{0x5B}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
            std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF}, std::byte{0xAA}};
}
} // namespace

TEST_CASE("security regression: oversized definite text view length is rejected") {
    auto             buffer = uint64_max_length_text_with_one_payload_byte();
    auto             dec    = make_decoder(buffer);
    std::string_view decoded;

    auto result = dec(decoded);

    REQUIRE_FALSE(result);
    CHECK((result.error() == status_code::incomplete || result.error() == status_code::error));
}

TEST_CASE("security regression: oversized definite byte view length is rejected") {
    auto                              buffer = uint64_max_length_bstr_with_one_payload_byte();
    auto                              dec    = make_decoder(buffer);
    std::basic_string_view<std::byte> decoded;

    auto result = dec(decoded);

    REQUIRE_FALSE(result);
    CHECK((result.error() == status_code::incomplete || result.error() == status_code::error));
}

TEST_CASE("security regression: direct tagged class decode rejects wrong tag before payload") {
    std::vector<std::byte> buffer{std::byte{0xCD}, std::byte{0x01}}; // tag(13), uint(1)
    auto                   dec = make_decoder(buffer);
    SecurityTaggedDirect   decoded{};

    auto result = dec(decoded);

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::no_match_for_tag);
    CHECK_EQ(decoded.value, 0);
}

TEST_CASE("security regression: nullable variant decodes null optional alternative") {
    using nullable_variant = std::variant<std::optional<int>, std::string>;

    std::vector<std::byte> buffer{std::byte{0xF6}}; // null
    auto                   dec = make_decoder(buffer);
    nullable_variant       decoded;

    auto result = dec(decoded);

    REQUIRE(result);
    REQUIRE(std::holds_alternative<std::optional<int>>(decoded));
    CHECK_FALSE(std::get<std::optional<int>>(decoded).has_value());
}

TEST_CASE("security regression: catch-all tags cannot mix with exact tag alternatives") {
    using tag_variant = std::variant<as_tag_any, static_tag<1>>;

    CHECK_FALSE(valid_concept_mapping_v<tag_variant>);
}

TEST_CASE("security regression: non-contiguous byte string decode materializes safely") {
    std::deque<std::byte>  input{std::byte{0x42}, std::byte{0xAA}, std::byte{0xBB}};
    auto                   dec = make_decoder(input);
    std::vector<std::byte> decoded;

    auto result = dec(decoded);

    REQUIRE(result);
    CHECK_EQ(decoded, std::vector<std::byte>{std::byte{0xAA}, std::byte{0xBB}});
}

TEST_CASE("security regression: diagnostic handles empty input and empty containers in compact mode") {
    DiagnosticOptions compact{.row_options = {.format_by_rows = false}};

    {
        std::vector<std::byte> buffer;
        std::string            diagnostic;

        CHECK_NOTHROW(buffer_diagnostic(buffer, diagnostic, compact));
        CHECK_EQ(diagnostic, "[]");
    }

    {
        std::vector<std::byte> buffer{std::byte{0x80}, std::byte{0xA0}}; // [], {}
        std::string            diagnostic;

        CHECK_NOTHROW(buffer_diagnostic(buffer, diagnostic, compact));
        CHECK_EQ(diagnostic, "[[], {}]");
    }
}

TEST_CASE("security regression: diagnostic does not present truncated array as valid data") {
    DiagnosticOptions      compact{.row_options = {.format_by_rows = false}};
    std::vector<std::byte> buffer{std::byte{0x82}, std::byte{0x01}}; // array(2), uint(1), then EOF
    std::string            diagnostic;

    CHECK_THROWS(buffer_diagnostic(buffer, diagnostic, compact));
}

TEST_CASE("security regression: diagnostic escapes text syntax characters") {
    DiagnosticOptions      compact{.row_options = {.format_by_rows = false}};
    std::vector<std::byte> buffer{std::byte{0x63}, std::byte{'"'}, std::byte{'x'}, std::byte{'\n'}};
    std::string            diagnostic;

    buffer_diagnostic(buffer, diagnostic, compact);

    CHECK(diagnostic.find("\\\"") != std::string::npos);
    CHECK(diagnostic.find("\\n") != std::string::npos);
}

TEST_CASE("security regression: cddl root name is reserved before nested definitions") {
    std::string schema;

    cddl_schema_to<std::vector<SecurityRootItem>>(schema, {.row_options = {.format_by_rows = false}, .root_name = "SecurityRootItem"});

    const auto first_rule = schema.find("SecurityRootItem =");
    REQUIRE(first_rule != std::string::npos);

    std::size_t exact_root_rules = 0;
    for (std::size_t pos = schema.find("SecurityRootItem ="); pos != std::string::npos; pos = schema.find("SecurityRootItem =", pos + 1)) {
        if (pos == 0 || schema[pos - 1] == '\n') {
            ++exact_root_rules;
        }
    }
    CHECK_EQ(exact_root_rules, 1);
}

#ifdef CBOR_TAGS_ENABLE_HANGING_SECURITY_REGRESSIONS
TEST_CASE("security regression: cddl always_inline recursive aggregate below container root terminates") {
    std::string schema;

    cddl_schema_to<std::vector<SecurityRecursiveNode>>(
        schema, {.row_options = {.format_by_rows = false}, .always_inline = true, .root_name = "Root"});

    CHECK(schema.find("SecurityRecursiveNode") != std::string::npos);
}

TEST_CASE("security regression: diagnostic rejects excessive nesting depth") {
    std::vector<std::byte> buffer(4096, std::byte{0x81});
    buffer.push_back(std::byte{0x00});

    std::string diagnostic;
    CHECK_THROWS(buffer_diagnostic(buffer, diagnostic));
}
#endif
