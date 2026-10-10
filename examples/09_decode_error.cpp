#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cstddef>
#include <string>
#include <vector>

using namespace cbor::tags;

/**
input = tstr
*/
int main() {
    // Hold the encoded data that would normally be transferred to the receiver.
    std::vector<std::byte> buffer;

    // Define text that cannot be decoded as an integer.
    std::string source = "not an integer";

    // Encode the text value.
    auto encoder = make_encoder(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // Choose an integer destination for the text input.
    int received{};

    // Decode once and inspect the returned error instead of retrying.
    auto decoder = make_decoder(buffer);
    auto result = decoder(received);
    if (result) {
        return 2;
    }

    // This destination requires an integer major type.
    if (result.error() != status_code::no_match_for_int_on_buffer) {
        return 2;
    }
    return 0;
}
