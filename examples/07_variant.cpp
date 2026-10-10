#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cstddef>
#include <string>
#include <variant>
#include <vector>

using namespace cbor::tags;

/**
variant_values = (int / tstr, int / tstr)
*/
int main() {
    // Hold the encoded data that would normally be transferred to the receiver.
    std::vector<std::byte> buffer;

    // Define two alternatives with distinct CBOR major types.
    std::variant<int, std::string> number = 42;
    std::variant<int, std::string> text = std::string{"ready"};

    // Encode both selected alternatives.
    auto encoder = make_encoder(buffer);
    if (!encoder(number, text)) {
        return 1;
    }

    // Declare receivers that allow either alternative.
    std::variant<int, std::string> received_number;
    std::variant<int, std::string> received_text;

    // Select each alternative from its CBOR type.
    auto decoder = make_decoder(buffer);
    if (!decoder(received_number, received_text)) {
        return 1;
    }

    // Check both selected alternatives and their values.
    if (received_number != number || received_text != text || decoder.tell() != buffer.end()) {
        return 2;
    }
    return 0;
}
