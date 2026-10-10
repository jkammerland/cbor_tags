#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cstddef>
#include <cstdint>
#include <vector>

using namespace cbor::tags;

/**
input = uint
*/
int main() {
    // Hold the encoded data that would normally be transferred to the receiver.
    std::vector<std::byte> buffer;

    // Define an unsigned integer that exceeds the receiver's range.
    std::uint64_t source = std::uint64_t{1} << 32;

    // Encode the unsigned integer.
    auto encoder = make_encoder(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // Choose a narrower destination with an initial value.
    std::uint32_t received = 7;

    // Enable representability checks and attempt decoding once.
    auto decoder = make_decoder_with_options<strict_integer_decoder_options>(buffer);
    auto result = decoder(received);
    if (result) {
        return 2;
    }

    // A matching major type does not imply that the value fits the destination.
    if (result.error() != status_code::no_match_for_uint_on_buffer || received != 7) {
        return 2;
    }
    return 0;
}
