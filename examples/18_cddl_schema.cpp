#include "cbor_tags/extensions/cbor_visualization.h"

#include <iostream>
#include <string>

using namespace cbor::tags;

struct Record {
    int id{};
    std::string name;
};

/**
record = [int, tstr]
*/
int main() {
    // Generate a schema for the same struct shape introduced earlier.
    std::string schema;
    cddl::schema_to<Record>(schema,
        {.row_options = {.format_by_rows = false}, .root_name = "record"});

    // Compare the schema with the independently written CDDL above main.
    if (schema != "record = [int, tstr]") {
        return 2;
    }

    // Print the generated schema for the reader.
    std::cout << schema << '\n';
    return 0;
}
