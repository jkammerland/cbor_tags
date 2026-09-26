#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/cbor_visualization.h>
#include <doctest/doctest.h>
#include <tuple>
#include <vector>

namespace wire_type_example {

struct record {
    int value{};
};

using wire = std::tuple<cbor::tags::static_tag<60020>, int>;

template <typename Encoder> auto encode(Encoder &enc, const record &value) {
    return enc(wire{cbor::tags::static_tag<60020>{}, value.value});
}

template <typename Decoder> auto decode(Decoder &dec, record &&value) {
    wire wire_value;
    auto result = dec(wire_value);
    if (result) {
        value.value = std::get<1>(wire_value);
    }
    return result;
}

} // namespace wire_type_example

namespace cbor::tags::cddl {
template <> struct cddl_wire_type<wire_type_example::record> {
    using type = wire_type_example::wire;
};
} // namespace cbor::tags::cddl

TEST_CASE("CDDL wire type customization matches application dispatch at roots and in arrays") {
    using namespace cbor::tags;
    fmt::memory_buffer schema;
    cddl_schema_to<wire_type_example::record>(schema, {.row_options = {.format_by_rows = false}, .root_name = "item"});
    CHECK(fmt::to_string(schema) == "item = #6.60020(int)");
    schema.clear();
    cddl_schema_to<std::vector<wire_type_example::record>>(schema, {.row_options = {.format_by_rows = false}});
    CHECK(fmt::to_string(schema) == "root = [* #6.60020(int)]");
    const std::vector<unsigned char> input{0xd9, 0xea, 0x74, 0x01, 0x07};
    wire_type_example::record        output;
    auto                             dec = make_decoder(input);
    REQUIRE(dec(output));
    CHECK(output.value == 1);
    int following{};
    REQUIRE(dec(following));
    CHECK(following == 7);
    std::vector<unsigned char> encoded;
    REQUIRE(make_encoder(encoded)(output));
    CHECK(encoded == std::vector<unsigned char>{0xd9, 0xea, 0x74, 0x01});
}
