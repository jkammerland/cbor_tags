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
    // Encode a present value followed by an absent value.
    std::optional<int> present = 42;
    std::optional<int> absent;
    std::vector<std::byte> buffer;
    auto encoder = make_encoder(buffer);
    if (!encoder(present, absent)) {
        return 1;
    }

    // Decode CBOR null as an empty optional.
    std::optional<int> received_present;
    std::optional<int> received_absent = 99;
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
