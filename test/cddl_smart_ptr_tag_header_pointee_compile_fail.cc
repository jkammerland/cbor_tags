#include "cbor_tags/extensions/cbor_visualization.h"

#include <memory>
#include <string>

int main() {
    std::string schema;
    cbor::tags::cddl_schema_to<std::shared_ptr<cbor::tags::as_tag_any>>(schema);
}
