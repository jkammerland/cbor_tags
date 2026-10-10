#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/codec/unique_ptr.h"

#include <cstddef>
#include <memory>
#include <vector>

using namespace cbor::tags;

/**
pointers = (int / null, int / null)
*/
int main() {
    // Opt into encoding a pointee as its value and an empty pointer as null.
    auto source = std::make_unique<int>(42);
    std::unique_ptr<int> empty;
    std::vector<std::byte> buffer;
    auto encoder = make_encoder<codec::unique_ptr>(buffer);
    if (!encoder(source, empty)) {
        return 1;
    }

    // Decode a new owned value followed by an empty pointer.
    std::unique_ptr<int> received;
    auto received_empty = std::make_unique<int>(99);
    auto decoder = make_decoder<codec::unique_ptr>(buffer);
    if (!decoder(received, received_empty)) {
        return 1;
    }

    // Check pointee values and the empty state instead of pointer identity.
    if (!received || *received != *source || received_empty || decoder.tell() != buffer.end()) {
        return 2;
    }
    return 0;
}
