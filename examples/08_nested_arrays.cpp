#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cstddef>
#include <vector>

using namespace cbor::tags;

/**
nested_arrays = [* [* int]]
*/
int main() {
    // Hold the encoded data that would normally be transferred to the receiver.
    std::vector<std::byte> buffer;

    // Compose an outer array from arrays introduced earlier.
    std::vector<std::vector<int>> source{{1, 2}, {3}};

    // Encode the nested containers as nested arrays.
    auto encoder = make_encoder(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // Declare a receiver with the same nested container shape.
    std::vector<std::vector<int>> received;

    // Decode the nested arrays into the receiver.
    auto decoder = make_decoder(buffer);
    if (!decoder(received)) {
        return 1;
    }

    // Compare the complete nested value.
    if (received != source || decoder.tell() != buffer.end()) {
        return 2;
    }
    return 0;
}
