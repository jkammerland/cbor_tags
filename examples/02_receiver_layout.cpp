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
    // The sender owns its text in a std::string.
    std::string source = "Hello, World!";
    std::vector<std::byte> buffer;
    auto encoder = make_encoder(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // The receiver chooses a view while keeping the encoded buffer alive and unchanged.
    std::string_view received_text;
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
