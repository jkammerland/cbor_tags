#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/cbor_visualization.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <functional>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

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

TEST_CASE("security regression: tag-only tuples are not valid tagged payloads") {
    using tag_only_tuple = std::tuple<static_tag<7>>;

    CHECK_FALSE(IsTaggedTuple<tag_only_tuple>);
    CHECK_FALSE(IsUntaggedTuple<tag_only_tuple>);
}

TEST_CASE("security regression: non-contiguous byte string decode materializes safely") {
    std::deque<std::byte>  input{std::byte{0x42}, std::byte{0xAA}, std::byte{0xBB}};
    auto                   dec = make_decoder(input);
    std::vector<std::byte> decoded;

    auto result = dec(decoded);

    REQUIRE(result);
    CHECK_EQ(decoded, std::vector<std::byte>{std::byte{0xAA}, std::byte{0xBB}});
}

TEST_CASE("security regression: non-contiguous byte view iterators are not produced from non-borrowed temporaries") {
    std::deque<char>           input{'\x43', '\x01', '\x02', '\x03'};
    auto                       dec = make_decoder(input);
    decltype(dec)::bstr_view_t view{};

    auto result = dec(view);
    REQUIRE(result);

    using transformed_view_t = decltype(view.view());
    CHECK(std::ranges::borrowed_range<transformed_view_t>);
}

TEST_CASE("security regression: variant simple accepts cbor undefined") {
    using simple_variant = std::variant<simple, int>;

    std::vector<std::byte> buffer{std::byte{0xF7}}; // undefined, #7.23
    auto                   dec = make_decoder(buffer);
    simple_variant         decoded{int{}};

    auto result = dec(decoded);

    REQUIRE(result);
    REQUIRE(std::holds_alternative<simple>(decoded));
    CHECK_EQ(std::get<simple>(decoded).value, 23);
}

TEST_CASE("security regression: optional simple does not hide bool variant alternatives") {
    using simple_or_bool = std::variant<std::optional<simple>, bool>;

    std::vector<std::byte> buffer{std::byte{0xF4}}; // false, #7.20
    auto                   dec = make_decoder(buffer);
    simple_or_bool         decoded{std::optional<simple>{}};

    auto result = dec(decoded);

    REQUIRE(result);
    REQUIRE(std::holds_alternative<bool>(decoded));
    CHECK_FALSE(std::get<bool>(decoded));
}

TEST_CASE("security regression: nested optional variant accepts simple alternatives") {
    using nested_optional_variant = std::variant<std::optional<std::variant<bool, int>>, std::string>;

    std::vector<std::byte>  buffer{std::byte{0xF4}}; // false, #7.20
    auto                    dec = make_decoder(buffer);
    nested_optional_variant decoded{std::string{}};

    auto result = dec(decoded);

    REQUIRE(result);
    REQUIRE(std::holds_alternative<std::optional<std::variant<bool, int>>>(decoded));
    const auto &optional_value = std::get<std::optional<std::variant<bool, int>>>(decoded);
    REQUIRE(optional_value.has_value());
    REQUIRE(std::holds_alternative<bool>(*optional_value));
    CHECK_FALSE(std::get<bool>(*optional_value));
}

TEST_CASE("security regression: catch-all simple variant accepts bool simple values") {
    using simple_variant = std::variant<simple, int>;

    std::vector<std::byte> buffer{std::byte{0xF4}}; // false, #7.20
    auto                   dec = make_decoder(buffer);
    simple_variant         decoded{int{}};

    auto result = dec(decoded);

    REQUIRE(result);
    REQUIRE(std::holds_alternative<simple>(decoded));
    CHECK_EQ(std::get<simple>(decoded).value, static_cast<simple::value_type>(SimpleType::Bool_False));
}

TEST_CASE("security regression: dynamic tagged tuples are rejected in variants") {
    using dynamic_tagged_tuple = std::tuple<dynamic_tag<std::uint64_t>, int>;
    using tag_variant          = std::variant<dynamic_tagged_tuple, std::string>;

    CHECK_FALSE(valid_concept_mapping_v<tag_variant>);
    CHECK_EQ(valid_concept_mapping_array_v<tag_variant>[detail::MajorIndex::DynamicTag], 1);
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

TEST_CASE("security regression: diagnostic renders cbor undefined as undefined") {
    DiagnosticOptions      compact{.row_options = {.format_by_rows = false}};
    std::vector<std::byte> buffer{std::byte{0xF7}}; // undefined, #7.23
    std::string            diagnostic;

    buffer_diagnostic(buffer, diagnostic, compact);

    CHECK_EQ(diagnostic, "[undefined]");
}

TEST_CASE("security regression: diagnostic rejects excessive nesting depth") {
    std::vector<std::byte> buffer(128, std::byte{0x81}); // array(1) nested 128 deep
    buffer.push_back(std::byte{0x00});

    std::string diagnostic;

    CHECK_THROWS(buffer_diagnostic(buffer, diagnostic));
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

TEST_CASE("security regression: cddl always_inline recursive aggregate below container root terminates") {
#if defined(__unix__) || defined(__APPLE__)
    const auto pid = fork();
    REQUIRE(pid >= 0);

    if (pid == 0) {
        if (const int null_fd = open("/dev/null", O_WRONLY); null_fd >= 0) {
            static_cast<void>(dup2(null_fd, STDOUT_FILENO));
            static_cast<void>(dup2(null_fd, STDERR_FILENO));
            close(null_fd);
        }
        alarm(3);

        std::string schema;
        cddl_schema_to<std::vector<SecurityRecursiveNode>>(
            schema, {.row_options = {.format_by_rows = false}, .always_inline = true, .root_name = "Root"});

        const bool has_recursive_rule = schema.find("SecurityRecursiveNode") != std::string::npos;
        _exit(has_recursive_rule ? 0 : 2);
    }

    int status = 0;
    REQUIRE_EQ(waitpid(pid, &status, 0), pid);
    CHECK(WIFEXITED(status));
    if (WIFEXITED(status)) {
        CHECK_EQ(WEXITSTATUS(status), 0);
    }
#else
    MESSAGE("fork-based recursion regression skipped on this platform");
#endif
}

TEST_CASE("security regression: cddl root name is reserved for external contexts") {
    detail::CDDLContext context;
    std::string         schema;

    cddl_schema_to<std::vector<SecurityRootItem>>(schema, {.row_options = {.format_by_rows = false}, .root_name = "SecurityRootItem"},
                                                  std::ref(context));

    CHECK_NE(schema, "SecurityRootItem = [* SecurityRootItem]");
    CHECK(schema.find("[* SecurityRootItem]") == std::string::npos);
}
