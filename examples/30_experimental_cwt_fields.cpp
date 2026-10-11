#include <cassert>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/codec/cwt.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * claims = { 1: tstr, ? "role": tstr }
 */
int main() {
    using namespace cbor::tags;

    // Experimental: the sender selects the fields it wants to transmit.
    std::string issuer = "device.example";
    std::optional<std::string> role = "reader";
    auto source = cwt::fields(cwt::field<1>(issuer), cwt::field<"role">(role));

    std::vector<std::byte> buffer;

    auto encoder = make_encoder<codec::cwt>(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // The receiver selects its layout and borrows the text from the buffer.
    std::string_view received_issuer;
    auto destination = cwt::fields(cwt::field<1>(received_issuer));

    auto decoder = make_decoder<codec::cwt>(buffer);
    if (!decoder(destination)) {
        return 1;
    }

    assert(received_issuer == issuer);
}
