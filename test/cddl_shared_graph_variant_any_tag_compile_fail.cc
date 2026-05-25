#include "cbor_tags/cbor.h"
#include "cbor_tags/extensions/cbor_visualization.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <fmt/format.h>
#include <memory>
#include <string>
#include <variant>

int main() {
    using value_type =
        cbor::tags::ext::smart_ptr::shared_graph_cddl<std::variant<std::shared_ptr<int>, cbor::tags::as_tag_any, std::string>>;

    fmt::memory_buffer buffer;
    cbor::tags::cddl_schema_to<value_type>(buffer, {.row_options = {.format_by_rows = false}});
    return 0;
}
