// Experimental: CWT models and codec APIs may change.
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/codec/cwt.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace cbor::tags;

namespace app {

struct issued_claims {
    std::string user;
    std::int64_t expires_at;
};

struct received_claims {
    std::string_view name;
    std::int64_t deadline;
};

// Map application fields to the registered CWT claim labels.
template <typename Encoder> auto encode(Encoder &encoder, const issued_claims &source) {
    const cwt::claims_view fields{
        .subject = source.user,
        .expiration = source.expires_at,
    };
    return encoder(fields);
}

// Require a subject and integer expiry before assigning the receiver's fields.
template <typename Decoder>
auto decode(Decoder &decoder, received_claims &&destination) -> typename Decoder::expected_type {
    cwt::claims_view fields;
    auto result = decoder(fields);
    if (!result) {
        return result;
    }
    if (!fields.subject || !fields.expiration) {
        return unexpected<status_code>{status_code::error};
    }

    const auto *expiry = std::get_if<std::int64_t>(&*fields.expiration);
    if (!expiry) {
        return unexpected<status_code>{status_code::error};
    }

    destination = {.name = *fields.subject, .deadline = *expiry};
    return result;
}

}

/**
claims = { 2 => tstr, 4 => int }
*/
int main() {
    // Keep the encoded claims alive while the receiver borrows their text.
    std::vector<std::byte> buffer;

    // Store the sender's values using application-defined names and owning text.
    app::issued_claims source{
        .user = "alice",
        .expires_at = 1893456000,
    };

    // Encode the application object through its ordinary customization function.
    auto encoder = make_encoder<codec::cwt>(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // Choose different field names and borrowed text for the receiver.
    app::received_claims received{};

    // Decode one claims map directly into the receiver's application type.
    auto decoder = make_decoder<codec::cwt>(buffer);
    if (!decoder(received) || decoder.tell() != buffer.end()) {
        return 1;
    }

    // Check the values without requiring matching sender and receiver layouts.
    if (received.name != source.user || received.deadline != source.expires_at) {
        return 2;
    }

    // Copy only the name if it must outlive the encoded buffer.
    const std::string retained_name{received.name};
    if (retained_name != source.user) {
        return 2;
    }
    return 0;
}
