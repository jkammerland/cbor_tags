#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cstddef>
#include <vector>

using namespace cbor::tags;

/**
nested_arrays = [* [* int]]
*/
int main() {
    // Compose an outer array from arrays already introduced earlier.
    std::vector<std::vector<int>> source{{1, 2}, {3}};
    std::vector<std::byte> buffer;
    auto encoder = make_encoder(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // Decode into the same nested container shape.
    std::vector<std::vector<int>> received;
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
