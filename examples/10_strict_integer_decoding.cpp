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
    // Encode an unsigned integer that exceeds the receiver's range.
    std::uint64_t source = std::uint64_t{1} << 32;
    std::vector<std::byte> buffer;
    auto encoder = make_encoder(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // Enable representability checks for the narrower destination.
    std::uint32_t received = 7;
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
