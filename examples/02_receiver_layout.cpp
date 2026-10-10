#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

using namespace cbor::tags;

/**
receiver_layout = tstr
*/
int main() {
    // Hold the encoded data that would normally be transferred to the receiver.
    std::vector<std::byte> buffer;

    // The sender owns its text in a std::string.
    std::string source = "Hello, World!";

    // Encode the owning string as CBOR text.
    auto encoder = make_encoder(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // Choose a text view that borrows from the stable encoded buffer.
    std::string_view received_text;

    // Decode the text directly into the receiver's chosen layout.
    auto decoder = make_decoder(buffer);
    if (!decoder(received_text)) {
        return 1;
    }

    // Both C++ representations use the same CBOR text-string type.
    if (received_text != source || decoder.tell() != buffer.end()) {
        return 2;
    }
    return 0;
}
