// Experimental: CWT models and codec APIs may change.
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/codec/cwt.h"

#include <cstddef>
#include <cstdint>
#include <vector>

using namespace cbor::tags;

/**
claims = {
    1 => tstr,
    2 => tstr,
    4 => int
}
*/
int main() {
    // Hold the encoded claims that would normally become a COSE message's payload.
    std::vector<std::byte> buffer;

    // Store issuer, subject and expiry as CWT claims, with time in Unix seconds.
    cwt::claims_set source{
        .issuer = "coap://as.example.com",
        .subject = "alice",
        .expiration = std::int64_t{1893456000},
    };

    // Encode the claim map with the opt-in CWT codec.
    auto encoder = make_encoder<codec::cwt>(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // Store decoded claims in a claims_set that owns its strings.
    cwt::claims_set received;

    // Decode the claims, copying strings from the buffer.
    auto decoder = make_decoder<codec::cwt>(buffer);
    if (!decoder(received)) {
        return 1;
    }

    // Check the three claim values and complete consumption.
    if (received.issuer != source.issuer || received.subject != source.subject ||
        received.expiration != source.expiration || decoder.tell() != buffer.end()) {
        return 2;
    }
    return 0;
}
