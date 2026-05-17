#include "cbor_tags/extensions/cbor_visualization.h"
#include "cbor_tags/extensions/rfc8746_typed_arrays.h"

#include <cstdint>
#include <fmt/format.h>
#include <variant>

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::rfc8746;

    fmt::memory_buffer buffer;
    cddl_schema_to<std::variant<typed_array<std::int32_t>, as_tag_any>>(buffer, {.row_options = {.format_by_rows = false}});
    return 0;
}
