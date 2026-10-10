#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cstddef>
#include <vector>

using namespace cbor::tags;

/**
numbers = [* int]
*/
int main() {
    // Hold the encoded data that would normally be transferred to the receiver.
    std::vector<std::byte> buffer;

    // Define the array elements.
    std::vector<int> source{1, 2, 3};

    // Encode the array without a length in its header.
    auto encoder = make_encoder(buffer);
    if (!encoder(as_indefinite{source})) {
        return 1;
    }

    // Declare an owning receiver for the array.
    std::vector<int> received;

    // Decode through the closing break with the ordinary container decoder.
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
