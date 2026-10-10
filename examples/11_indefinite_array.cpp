#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cstddef>
#include <vector>

using namespace cbor::tags;

/**
numbers = [* int]
*/
int main() {
    // Encode the array without a length in its header.
    std::vector<int> source{1, 2, 3};
    std::vector<std::byte> buffer;
    auto encoder = make_encoder(buffer);
    if (!encoder(as_indefinite{source})) {
        return 1;
    }

    // The ordinary container decoder consumes elements through the closing break.
    std::vector<int> received;
    auto decoder = make_decoder(buffer);
    if (!decoder(received)) {
        return 1;
    }

    // Compare values independently of their definite or indefinite encoding.
    if (received != source || decoder.tell() != buffer.end()) {
        return 2;
    }
    return 0;
}
