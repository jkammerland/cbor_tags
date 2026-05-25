#include "cbor_tags/extensions/cbor_visualization.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <fmt/format.h>
#include <memory>

struct BadScopedMember {
    cbor::tags::ext::smart_ptr::shared_graph_cddl<std::shared_ptr<int>> value;
};

int main() {
    fmt::memory_buffer buffer;
    cbor::tags::cddl_schema_to<BadScopedMember>(buffer, {.row_options = {.format_by_rows = false}});
    return 0;
}
