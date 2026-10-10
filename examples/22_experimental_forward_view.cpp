// Experimental: raw-view and segment APIs may change.
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_raw_views.h"
#include "cbor_tags/cbor_segments.h"

#include <cstddef>
#include <vector>

using namespace cbor::tags;

/**
numbers = [* int]
*/
int main() {
    // Hold the encoded input while its borrowed views are used.
    std::vector<std::byte> buffer;

    // Define an array as a stand-in for data received from another system.
    std::vector<int> source{1, 2, 3};

    // Encode the array into the input buffer.
    auto encoder = make_encoder(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // Declare a receiver that views the complete encoded array, including its header.
    encoded_array_view_for<decltype(buffer)> encoded;

    // Locate the encoded item without materializing its elements.
    auto decoder = make_decoder(buffer);
    if (!decoder(encoded)) {
        return 1;
    }

    // Forward the existing header and payload as one view into the original buffer.
    auto forwarded = borrow_segments(encoded);

    // Check that forwarding borrows the complete original item.
    if (forwarded.size() != 1 || !forwarded[0].is_borrowed() ||
        forwarded[0].bytes().data() != buffer.data() ||
        forwarded[0].bytes().size() != buffer.size() || decoder.tell() != buffer.end()) {
        return 2;
    }

    // Use the forwarded byte view as the verification decoder's input.
    auto forwarded_bytes = forwarded[0].bytes();

    // Declare an owning receiver solely to verify the forwarded array's values.
    std::vector<int> received;

    // Decode the forwarded array for verification.
    auto verifier = make_decoder(forwarded_bytes);
    if (!verifier(received)) {
        return 1;
    }

    // Check the forwarded values and complete consumption.
    if (received != source || verifier.tell() != forwarded_bytes.end()) {
        return 2;
    }
    return 0;
}
