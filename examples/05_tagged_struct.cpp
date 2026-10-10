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
    // The static tag identifies the record without becoming an array member.
    Record source{42, "ready"};
    std::vector<std::byte> buffer;
    auto encoder = make_encoder(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // Decode the tagged array into a new record.
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
