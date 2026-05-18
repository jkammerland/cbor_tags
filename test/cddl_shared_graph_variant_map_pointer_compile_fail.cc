#include "cbor_tags/extensions/cbor_visualization.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <fmt/format.h>
#include <map>
#include <memory>
#include <string>
#include <variant>

int main() {
    using value_type = cbor::tags::ext::smart_ptr::shared_graph_cddl<std::variant<std::map<int, std::shared_ptr<int>>, std::string>>;

    fmt::memory_buffer buffer;
    cbor::tags::cddl_schema_to<value_type>(buffer, {.row_options = {.format_by_rows = false}});
    return 0;
}
