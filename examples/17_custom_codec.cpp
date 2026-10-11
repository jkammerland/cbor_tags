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

// This codec encodes and decodes Temperature as one CBOR integer.
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
    app::Temperature source{.degrees = 21};

    // Install the application codec explicitly on the encoder.
    auto encoder = make_encoder<app::codec::temperature>(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // Declare receivers for the ordinary integer and application value.
    int decoded_integer{};
    app::Temperature received;

    // Decode the same data as an ordinary integer.
    auto integer_decoder = make_decoder(buffer);
    if (!integer_decoder(decoded_integer)) {
        return 2;
    }

    // Check the integer representation and complete consumption.
    if (decoded_integer != source.degrees || integer_decoder.tell() != buffer.end()) {
        return 2;
    }

    // Use the same codec to decode the integer into Temperature.
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
