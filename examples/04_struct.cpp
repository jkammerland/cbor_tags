#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cstddef>
#include <string>
#include <vector>

using namespace cbor::tags;

struct Record {
    int id{};
    std::string name;
};

/**
record = [int, tstr]
*/
int main() {
    // Hold the encoded data that would normally be transferred to the receiver.
    std::vector<std::byte> buffer;

    // Define a record with public aggregate members.
    Record source{.id = 42, .name = "ready"};

    // Encode the members as an array in declaration order.
    auto encoder = make_encoder(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // Declare a record to receive the array members.
    Record received;

    // Decode the array into the receiver.
    auto decoder = make_decoder(buffer);
    if (!decoder(received)) {
        return 1;
    }

    // Check each reflected member.
    if (received.id != source.id || received.name != source.name ||
        decoder.tell() != buffer.end()) {
        return 2;
    }
    return 0;
}
