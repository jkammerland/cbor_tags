#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_traversal.h"

#include <cstddef>
#include <string>
#include <vector>

using namespace cbor::tags;

/**
items = (tstr, int)
*/
int main() {
    // Encode one item to validate followed by another item.
    std::string text = "ready";
    int following = 42;
    std::vector<std::byte> buffer;
    auto encoder = make_encoder(buffer);
    if (!encoder(text, following)) {
        return 1;
    }

    // Validate the next complete item without constructing its value.
    auto decoder = make_decoder(buffer);
    if (!validate_item(decoder)) {
        return 1;
    }

    // Validation leaves the following item unread.
    int received{};
    if (!decoder(received)) {
        return 1;
    }
    if (received != following || decoder.tell() != buffer.end()) {
        return 2;
    }
    return 0;
}
