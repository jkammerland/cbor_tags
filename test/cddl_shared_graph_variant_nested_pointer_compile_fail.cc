#include "cbor_tags/cbor.h"
#include "cbor_tags/extensions/cbor_visualization.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <fmt/format.h>
#include <memory>
#include <string>
#include <variant>

int main() {
    using nested_type = std::variant<std::shared_ptr<int>, std::string>;
    using value_type  = cbor::tags::ext::smart_ptr::shared_graph_cddl<std::variant<nested_type, cbor::tags::static_tag<42>>>;

    fmt::memory_buffer buffer;
    cbor::tags::cddl_schema_to<value_type>(buffer, {.row_options = {.format_by_rows = false}});
    return 0;
}
