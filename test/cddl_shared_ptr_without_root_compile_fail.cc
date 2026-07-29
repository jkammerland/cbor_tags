#include "cbor_tags/extensions/cbor_visualization.h"

#include <fmt/format.h>
#include <memory>
#include <string>
#include <variant>

int main() {
    fmt::memory_buffer buffer;
    cbor::tags::cddl_schema_to<std::variant<std::shared_ptr<int>, std::string>>(buffer, {.row_options = {.format_by_rows = false}});
    return 0;
}
