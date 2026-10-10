#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cstddef>
#include <optional>
#include <vector>

using namespace cbor::tags;

/**
optional_values = (int / null, int / null)
*/
int main() {
    // Hold the encoded data that would normally be transferred to the receiver.
    std::vector<std::byte> buffer;

    // Define a present value and an absent value.
    std::optional<int> present = 42;
    std::optional<int> absent;

    // Encode the present value followed by CBOR null.
    auto encoder = make_encoder(buffer);
    if (!encoder(present, absent)) {
        return 1;
    }

    // Declare receivers for both optional states.
    std::optional<int> received_present;
    std::optional<int> received_absent = 99;

    // Decode CBOR null as an empty optional.
    auto decoder = make_decoder(buffer);
    if (!decoder(received_present, received_absent)) {
        return 1;
    }

    // Check both optional states.
    if (received_present != present || received_absent.has_value() ||
        decoder.tell() != buffer.end()) {
        return 2;
    }
    return 0;
}
