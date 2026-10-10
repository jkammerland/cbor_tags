#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

using namespace cbor::tags;

/**
containers = ([* int], {* tstr => int})
*/
int main() {
    // Hold the encoded data that would normally be transferred to the receiver.
    std::vector<std::byte> buffer;

    // Define an array of numbers and a map of named counts.
    std::vector<int> numbers{1, 2, 3};
    std::map<std::string, int> counts{{"apples", 2}, {"pears", 3}};

    // Encode the vector as one array and the map as one key/value item.
    auto encoder = make_encoder(buffer);
    if (!encoder(numbers)) {
        return 1;
    }
    if (!encoder(counts)) {
        return 1;
    }

    // Declare receivers for the array and map.
    std::vector<int> decoded_numbers;
    std::map<std::string, int> decoded_counts;

    // Decode the two items in the same order.
    auto decoder = make_decoder(buffer);
    if (!decoder(decoded_numbers, decoded_counts)) {
        return 1;
    }

    // Check both containers and complete consumption.
    if (decoded_numbers != numbers || decoded_counts != counts || decoder.tell() != buffer.end()) {
        return 2;
    }
    return 0;
}
