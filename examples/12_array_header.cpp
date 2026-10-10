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

    // Define the elements of a definite array.
    std::vector<int> source{1, 2, 3};

    // Encode the array with its element count in the header.
    auto encoder = make_encoder(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // Declare separate receivers for the header and elements.
    as_array_any header;
    std::vector<int> received;

    // Read only the header, leaving the elements unread.
    auto decoder = make_decoder(buffer);
    if (!decoder(header)) {
        return 1;
    }

    // Check the declared count before reading the elements.
    if (header.indefinite || header.size != source.size()) {
        return 2;
    }

    // Read each declared element individually.
    for (std::size_t index = 0; index < header.size; ++index) {
        int value{};
        if (!decoder(value)) {
            return 1;
        }
        received.push_back(value);
    }

    // Check the reconstructed array and complete consumption.
    if (received != source || decoder.tell() != buffer.end()) {
        return 2;
    }
    return 0;
}
