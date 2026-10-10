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
    // Public aggregate members are encoded as an array in declaration order.
    Record source{42, "ready"};
    std::vector<std::byte> buffer;
    auto encoder = make_encoder(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // Decode the array into a new record.
    Record received;
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
