// Experimental: raw-view and segment APIs may change.
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_segments.h"

#include <cstddef>
#include <vector>

using namespace cbor::tags;

/**
numbers = [* int]
*/
int main() {
    // Prepare an encoded array as a stand-in for data received from another system.
    std::vector<int> source{1, 2, 3};
    std::vector<std::byte> buffer;
    auto encoder = make_encoder(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // Decode a view of the complete encoded item, including its header.
    auto decoder = make_decoder(buffer);
    decltype(decoder)::raw_encoded_array_view encoded;
    if (!decoder(encoded)) {
        return 1;
    }

    // Forward the existing header and payload as one view into the original buffer.
    auto forwarded = borrow_segments(encoded);
    if (forwarded.size() != 1 || !forwarded[0].is_borrowed() ||
        forwarded[0].bytes().data() != buffer.data() ||
        forwarded[0].bytes().size() != buffer.size() || decoder.tell() != buffer.end()) {
        return 2;
    }

    // Materialize values only to verify that the forwarded bytes remain a valid array.
    std::vector<int> received;
    auto forwarded_bytes = forwarded[0].bytes();
    auto verifier = make_decoder(forwarded_bytes);
    if (!verifier(received)) {
        return 1;
    }
    if (received != source || verifier.tell() != forwarded_bytes.end()) {
        return 2;
    }
    return 0;
}
