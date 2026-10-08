#include "cbor_tags/codec/typed_array.h"
#include "cbor_tags/extensions/cbor_visualization.h"

#include <cstdint>
#include <fmt/format.h>
#include <variant>

int main() {
    using namespace cbor::tags::rfc8746;

    fmt::memory_buffer buffer;
    cbor::tags::cddl::schema_to<std::variant<typed_array<std::int32_t>, typed_array_view<std::int32_t>>>(
        buffer, {.row_options = {.format_by_rows = false}});
    return 0;
}
