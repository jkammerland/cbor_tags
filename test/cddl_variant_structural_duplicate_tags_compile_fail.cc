#include "cbor_tags/codec/typed_array.h"
#include "cbor_tags/extensions/cbor_visualization.h"

#include <fmt/format.h>
#include <variant>
#include <vector>

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::rfc8746;

    fmt::memory_buffer buffer;
    cddl::schema_to<std::variant<homogeneous_array<std::vector<int>>, static_tag<41>>>(buffer, {.row_options = {.format_by_rows = false}});
    return 0;
}
