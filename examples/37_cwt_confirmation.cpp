// Experimental: public key and confirmation APIs may change.
#include <cassert>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/codec/cwt.h>

/**
 * confirmation = { 3: bstr }
 * claims = { 8: confirmation }
 */
int main() {
    using namespace cbor::tags;

    // A key identifier names a key that the application resolves separately.
    cwt::confirmation<cwt::ec2_public_key> source;
    source.kid = std::vector{std::byte{0x42}};
    auto claims = cwt::fields(cwt::field<8>(source));

    std::vector<std::byte> buffer;

    auto encoder = make_encoder<codec::cwt>(buffer);
    if (!encoder(claims)) {
        return 1;
    }

    cwt::confirmation_view<cwt::ec2_public_key_view> received;
    auto received_claims = cwt::fields(cwt::field<8>(received));
    auto decoder = make_decoder<codec::cwt>(buffer);
    if (!decoder(received_claims)) {
        return 1;
    }

    // Decoding confirmation metadata does not prove possession of the named key.
    assert(received.kid && received.kid->front() == std::byte{0x42});
}
