#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/cbor_visualization.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class consumer_color : unsigned { red = 1, blue = 2 };

struct consumer_point {
    std::uint64_t x;
    std::uint64_t y;
};

int main() {
    using namespace cbor::tags;

    std::string enum_schema;
    cddl_schema_to<consumer_color>(enum_schema, {.enum_mode = CDDLEnumMode::named_values});
    if (enum_schema.find("red") == std::string::npos || enum_schema.find("blue") == std::string::npos) {
        return 1;
    }

    std::string map_schema;
    cddl_schema_to<as_named_map<consumer_point>>(map_schema, {.row_options = {.format_by_rows = false}, .root_name = "point"});
    if (map_schema.find("x: uint") == std::string::npos || map_schema.find("y: uint") == std::string::npos) {
        return 2;
    }

    std::vector<std::byte> buffer;
    auto                   enc   = make_encoder(buffer);
    consumer_point         value = {.x = 1, .y = 2};
    if (!enc(as_named_map{value})) {
        return 3;
    }

    consumer_point decoded{};
    auto           dec = make_decoder(buffer);
    if (!dec(as_named_map{decoded})) {
        return 4;
    }

    return decoded.x == 1 && decoded.y == 2 ? 0 : 5;
}
