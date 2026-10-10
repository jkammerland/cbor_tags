#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cstddef>
#include <string>
#include <vector>

using namespace cbor::tags;

/**
basic_values = (int, tstr, bool, float64, bstr)
*/
int main() {
    // Hold the encoded data that would normally be transferred to the receiver.
    std::vector<std::byte> buffer;

    // Prepare five values with different CBOR types.
    int number = 42;
    std::string text = "Hello, World!";
    bool enabled = true;
    double measurement = 3.5;
    std::vector<std::byte> bytes{std::byte{0x01}, std::byte{0x02}};

    // Encode consecutive items without grouping them into an array.
    auto encoder = make_encoder(buffer);
    if (!encoder(number, text, enabled, measurement, bytes)) {
        return 1;
    }

    // Declare a receiver for each encoded value.
    int decoded_number{};
    std::string decoded_text;
    bool decoded_enabled{};
    double decoded_measurement{};
    std::vector<std::byte> decoded_bytes;

    // Decode the items in the same order.
    auto decoder = make_decoder(buffer);
    if (!decoder(decoded_number, decoded_text, decoded_enabled, decoded_measurement,
            decoded_bytes)) {
        return 1;
    }

    // Check the values and that all five items were consumed.
    if (decoded_number != number || decoded_text != text || decoded_enabled != enabled ||
        decoded_measurement != measurement || decoded_bytes != bytes ||
        decoder.tell() != buffer.end()) {
        return 2;
    }
    return 0;
}
