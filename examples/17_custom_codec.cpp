#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/codec.h"

#include <cstddef>
#include <vector>

using namespace cbor::tags;

namespace app {

struct Temperature {
    int degrees{};
};

namespace codec {

// This codec gives Temperature the wire representation of one integer.
template <typename Self> struct temperature : cbor::tags::codec::base<Self> {
    using base = cbor::tags::codec::base<Self>;
    using base::decode;
    using base::encode;

    void encode(const Temperature &value) {
        auto &encoder = static_cast<Self &>(*this);
        encoder.encode(value.degrees);
    }

    status_code decode(Temperature &value, major_type major, std::byte additional_info) {
        auto &decoder = static_cast<Self &>(*this);
        return decoder.decode(value.degrees, major, additional_info);
    }
};

}
}

/**
temperature = int
*/
int main() {
    // Hold the encoded data that would normally be transferred to the receiver.
    std::vector<std::byte> buffer;

    // Define an application temperature value.
    app::Temperature source{21};

    // Install the application codec explicitly on the encoder.
    auto encoder = make_encoder<app::codec::temperature>(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // Declare receivers for the ordinary integer and application value.
    int wire_value{};
    app::Temperature received;

    // The ordinary decoder also sees one integer on the wire.
    auto wire_decoder = make_decoder(buffer);
    if (!wire_decoder(wire_value)) {
        return 2;
    }

    // Check the integer representation and complete consumption.
    if (wire_value != source.degrees || wire_decoder.tell() != buffer.end()) {
        return 2;
    }

    // Install the same wire policy on the application decoder.
    auto decoder = make_decoder<app::codec::temperature>(buffer);
    if (!decoder(received)) {
        return 1;
    }

    // Check the application value reconstructed from the integer.
    if (received.degrees != source.degrees || decoder.tell() != buffer.end()) {
        return 2;
    }
    return 0;
}
