#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_traversal.h"

#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

using namespace cbor::tags;

/**
nested_arrays = [* [* int]]
*/
int main() {
    // Hold the encoded data that would normally be transferred to the receiver.
    std::vector<std::byte> buffer;

    // Define an outer array containing two inner arrays.
    std::vector<std::vector<int>> source{{1, 2}, {3}};

    // Encode the nested arrays into the buffer.
    auto encoder = make_encoder(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // Print value bytes at their nesting depth, with blank lines between completed groups.
    bool separate_group = false;
    auto print_bytes = [&separate_group](const auto &, const auto &context) {
        if (context.kind == walk_event_kind::leave) {
            separate_group = true;
            return;
        }
        if (context.kind != walk_event_kind::value) {
            return;
        }

        if (separate_group) {
            std::cout << '\n';
            separate_group = false;
        }

        for (auto byte : context.source) {
            std::cout << std::string(context.depth * 2, ' ') << std::hex << std::setfill('0')
                      << std::setw(2) << std::to_integer<unsigned>(byte) << '\n';
        }
    };

    // Walk the item with the named callback.
    auto decoder = make_decoder(buffer);
    if (!walk_item(decoder, print_bytes)) {
        return 1;
    }

    // Check that the complete item was consumed.
    if (decoder.tell() != buffer.end()) {
        return 2;
    }
    return 0;
}
