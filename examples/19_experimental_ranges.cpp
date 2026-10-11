// Experimental: range-wrapper APIs may change.
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_ranges.h"

#include <cstddef>
#include <ranges>
#include <vector>

using namespace cbor::tags;

/**
numbers = [* int]
*/
int main() {
    // Hold the encoded data that would normally be transferred to the receiver.
    std::vector<std::byte> buffer;

    // Filter existing values without collecting them into another vector.
    std::vector<int> source{1, 2, 3, 4, 5, 6};
    auto is_even = [](int value) { return value % 2 == 0; };
    auto evens = source | std::views::filter(is_even);

    // Encode the view as an array, writing CBOR bytes into the buffer.
    auto encoder = make_encoder(buffer);
    if (!encoder(as_array_range(evens))) {
        return 1;
    }

    // Store the decoded values in a vector.
    std::vector<int> received;

    // Decode the array into the receiver.
    auto decoder = make_decoder(buffer);
    if (!decoder(received)) {
        return 1;
    }

    // Check the independently specified filtered values and complete consumption.
    std::vector<int> expected{2, 4, 6};
    if (received != expected || decoder.tell() != buffer.end()) {
        return 2;
    }
    return 0;
}
