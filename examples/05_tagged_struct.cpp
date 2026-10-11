#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using namespace cbor::tags;

struct Record {
    static constexpr std::uint64_t cbor_tag = 1001;

    int id{};
    std::string name;
};

/**
record = #6.1001([int, tstr])
*/
int main() {
    // Hold the encoded data that would normally be transferred to the receiver.
    std::vector<std::byte> buffer;

    // Define a record with a static identifying tag.
    Record source{.id = 42, .name = "ready"};

    // Encode the tag followed by the record array.
    auto encoder = make_encoder(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // Declare a record to receive the tagged array.
    Record received;

    // Decode the tagged array into the receiver.
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
