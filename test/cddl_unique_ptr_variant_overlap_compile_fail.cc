#include "cbor_tags/extensions/cbor_visualization.h"

#include <cstdint>
#include <memory>
#include <string>
#include <variant>

int main() {
    std::string schema;
    cbor::tags::cddl_schema_to<std::variant<std::unique_ptr<std::uint64_t>, std::uint64_t>>(schema);
}
