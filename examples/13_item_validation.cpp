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
    // Hold the encoded data that would normally be transferred to the receiver.
    std::vector<std::byte> buffer;

    // Define one item to validate followed by another item.
    std::string text = "ready";
    int following = 42;

    // Encode both items into the buffer.
    auto encoder = make_encoder(buffer);
    if (!encoder(text, following)) {
        return 1;
    }

    // Declare a receiver for the item following the validated text.
    int received{};

    // Create a decoder over the encoded items.
    auto decoder = make_decoder(buffer);

    // Validate the next complete item without constructing its value.
    if (!validate_item(decoder)) {
        return 1;
    }

    // Decode the following item left unread by validation.
    if (!decoder(received)) {
        return 1;
    }

    // Check the following value and complete consumption.
    if (received != following || decoder.tell() != buffer.end()) {
        return 2;
    }
    return 0;
}
