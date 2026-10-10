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
    // Encode a vector as one array.
    std::vector<int> numbers{1, 2, 3};
    std::vector<std::byte> buffer;
    auto encoder = make_encoder(buffer);
    if (!encoder(numbers)) {
        return 1;
    }

    // Encode a map as one item containing key/value pairs.
    std::map<std::string, int> counts{{"apples", 2}, {"pears", 3}};
    if (!encoder(counts)) {
        return 1;
    }

    // Decode the array and the map into their respective containers.
    std::vector<int> decoded_numbers;
    std::map<std::string, int> decoded_counts;
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
