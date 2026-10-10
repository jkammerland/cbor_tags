#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cstddef>
#include <vector>

using namespace cbor::tags;

/**
numbers = [* int]
*/
int main() {
    // Encode a definite array whose header declares its element count.
    std::vector<int> source{1, 2, 3};
    std::vector<std::byte> buffer;
    auto encoder = make_encoder(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // Read only the header, leaving the elements unread.
    as_array_any header;
    auto decoder = make_decoder(buffer);
    if (!decoder(header)) {
        return 1;
    }
    if (header.indefinite || header.size != source.size()) {
        return 2;
    }

    // Read each declared element individually.
    std::vector<int> received;
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
