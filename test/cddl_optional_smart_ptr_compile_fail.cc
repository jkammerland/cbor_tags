#include "cbor_tags/extensions/cbor_visualization.h"

#include <memory>
#include <optional>
#include <string>

int main() {
    std::string schema;
    cbor::tags::cddl::schema_to<std::optional<std::unique_ptr<int>>>(schema);
}
