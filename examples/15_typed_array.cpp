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
    // Hold the encoded data that would normally be transferred to the receiver.
    std::vector<std::byte> buffer;

    // Define signed 32-bit array elements.
    std::vector<std::int32_t> source{1, 2, 3};

    // Opt into the typed-array codec for little-endian elements.
    auto encoder = make_encoder<codec::typed_array>(buffer);
    if (!encoder(rfc8746::as_typed_array(source))) {
        return 1;
    }

    // Declare an owning typed-array receiver.
    rfc8746::typed_array<std::int32_t> received;

    // Decode the tagged byte string with the typed-array codec.
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
