// Experimental: public key and confirmation APIs may change.
#include <cassert>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/codec/cwt.h>

/**
 * public-key = { 1: 1, -1: 6, -2: bstr .size 32 }
 */
int main() {
    using namespace cbor::tags;

    // Represent an Ed25519 public key without choosing a crypto provider.
    cwt::okp_public_key source;
    source.x = {
        std::byte{0xd7},
        std::byte{0x5a},
        std::byte{0x98},
        std::byte{0x01},
        std::byte{0x82},
        std::byte{0xb1},
        std::byte{0x0a},
        std::byte{0xb7},
        std::byte{0xd5},
        std::byte{0x4b},
        std::byte{0xfe},
        std::byte{0xd3},
        std::byte{0xc9},
        std::byte{0x64},
        std::byte{0x07},
        std::byte{0x3a},
        std::byte{0x0e},
        std::byte{0xe1},
        std::byte{0x72},
        std::byte{0xf3},
        std::byte{0xda},
        std::byte{0xa6},
        std::byte{0x23},
        std::byte{0x25},
        std::byte{0xaf},
        std::byte{0x02},
        std::byte{0x1a},
        std::byte{0x68},
        std::byte{0xf7},
        std::byte{0x07},
        std::byte{0x51},
        std::byte{0x1a},
    };

    std::vector<std::byte> buffer;

    auto encoder = make_encoder<codec::cwt>(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // The receiver borrows key bytes from its input buffer.
    cwt::okp_public_key_view received;
    auto decoder = make_decoder<codec::cwt>(buffer);
    if (!decoder(received)) {
        return 1;
    }

    assert(std::ranges::equal(received.x, source.x));
}
