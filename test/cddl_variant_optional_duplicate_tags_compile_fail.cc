#include "cbor_tags/extensions/cbor_visualization.h"
#include "cbor_tags/extensions/rfc8746_typed_arrays.h"

#include <cstdint>
#include <fmt/format.h>
#include <optional>
#include <variant>

int main() {
    using namespace cbor::tags::ext::rfc8746;

    using optional_ref = std::optional<typed_array_ref<std::int32_t>>;

    fmt::memory_buffer buffer;
    cbor::tags::cddl_schema_to<std::variant<optional_ref, typed_array<std::int32_t>>>(buffer, {.row_options = {.format_by_rows = false}});
    return 0;
}
