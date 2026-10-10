#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/codec/typed_array.h"

#include <cstddef>
#include <cstdint>
#include <vector>

using namespace cbor::tags;

/**
numbers = #6.78(bstr)
*/
int main() {
    // Opt into the typed-array codec for little-endian signed 32-bit elements.
    std::vector<std::int32_t> source{1, 2, 3};
    std::vector<std::byte> buffer;
    auto encoder = make_encoder<codec::typed_array>(buffer);
    if (!encoder(rfc8746::as_typed_array(source))) {
        return 1;
    }

    // Decode the tagged byte string into an owning typed array.
    rfc8746::typed_array<std::int32_t> received;
    auto decoder = make_decoder<codec::typed_array>(buffer);
    if (!decoder(received)) {
        return 1;
    }

    // Compare the numerical values rather than the byte-string representation.
    if (received.values() != source || decoder.tell() != buffer.end()) {
        return 2;
    }
    return 0;
}
