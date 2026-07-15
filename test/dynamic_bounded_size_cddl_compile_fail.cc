#include "cbor_tags/extensions/cbor_visualization.h"

#include <fmt/format.h>
#include <vector>

int main() {
    fmt::memory_buffer output;
    cbor::tags::cddl_schema_to<cbor::tags::dynamic_bounded_size<std::vector<int>>>(output);
    return 0;
}
