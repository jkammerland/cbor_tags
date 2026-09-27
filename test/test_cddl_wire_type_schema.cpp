#include <cbor_tags/extensions/cbor_visualization.h>
#include <doctest/doctest.h>
#include <fmt/format.h>

namespace wire_schema_test {

struct leaf {
    int value{};
};

struct model {
    leaf first;
    leaf second;
};

struct wrapper {};

enum class enum_wrapper { value };

} // namespace wire_schema_test

namespace cbor::tags::cddl {

template <> struct cddl_wire_type<wire_schema_test::wrapper> {
    using type = wire_schema_test::model;
};

template <> struct cddl_wire_type<wire_schema_test::enum_wrapper> {
    using type = int;
};

} // namespace cbor::tags::cddl

TEST_CASE("CDDL wire roots preserve dependent definitions and their order") {
    fmt::memory_buffer schema;
    cbor::tags::cddl_schema_to<wire_schema_test::wrapper>(schema, {.row_options = {.format_by_rows = false}, .root_name = "item"});
    CHECK(fmt::to_string(schema) == "item = model\nleaf = int\nmodel = [leaf, leaf]");

    schema.clear();
    cbor::tags::cddl_schema_to<wire_schema_test::wrapper>(
        schema, {.row_options = {.format_by_rows = false}, .always_inline = true, .root_name = "item"});
    CHECK(fmt::to_string(schema) == "item = [int, int]");
}

TEST_CASE("CDDL wire roots take precedence over enum naming") {
    fmt::memory_buffer schema;
    cbor::tags::cddl_schema_to<wire_schema_test::enum_wrapper>(
        schema, {.row_options = {.format_by_rows = false}, .root_name = "item", .enum_mode = cbor::tags::CDDLEnumMode::named_values});
    CHECK(fmt::to_string(schema) == "item = int");
}
