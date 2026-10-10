// Experimental: view-based byte-string encoding and decoding.
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>

using namespace cbor::tags;

/**
payload = bstr
*/
int main() {
    // Hold the encoded data that would normally be transferred to the receiver.
    std::vector<std::byte> buffer;

    // Define a byte view over existing source data.
    std::vector<std::byte> source{std::byte{1}, std::byte{2}, std::byte{3}};
    std::span<const std::byte> outgoing = source;

    // Encode the byte view, copying its bytes into the ordinary output buffer.
    auto encoder = make_encoder(buffer);
    if (!encoder(outgoing)) {
        return 1;
    }

    // Borrow the payload to avoid copying while keeping the encoded buffer stable.
    std::span<const std::byte> received;

    // Decode directly into the byte view.
    auto decoder = make_decoder(buffer);
    if (!decoder(received)) {
        return 1;
    }

    // Check both the values and that the view refers to the original encoded payload.
    const auto *payload_begin = buffer.data() + buffer.size() - source.size();
    if (!std::ranges::equal(received, source) || received.data() != payload_begin ||
        decoder.tell() != buffer.end()) {
        return 2;
    }
    return 0;
}
