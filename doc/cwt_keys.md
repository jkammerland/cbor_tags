# Experimental public keys and confirmation

Encode a public key and decode its bytes as a view ([example 36](../examples/36_experimental_cwt_public_keys.cpp)); this does not require a crypto backend.

```cpp
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
```

## Compute a thumbprint

This example uses the [sample P-256 key helper](../examples/cwt_key_sample.h); build with `CBOR_TAGS_ENABLE_CWT_OPENSSL=ON`.

```cpp
// Experimental: public key and thumbprint APIs may change.
#include "cwt_key_sample.h"

#include <cassert>

/**
 * public-key = { 1: 2, -1: 1, -2: bstr .size 32, -3: bstr .size 32, ? 3: int, ? 4: [* int] }
 * thumbprint-input = { 1: 2, -1: 1, -2: bstr .size 32, -3: bstr .size 32 }
 */
int main() {
    using namespace cbor::tags;

    auto key = example_holder_key();

    // Hash the required public parameters, excluding algorithm and operation restrictions.
    auto digest = cwt::key_thumbprint<cwt::openssl_sha256>(key);
    if (!digest) {
        return 1;
    }

    // Segments own ordered headers and borrow key bytes, useful for streaming hash providers.
    auto parts = cwt::make_key_thumbprint_input(key);
    if (!parts) {
        return 1;
    }

    assert(digest->size() == 32);
}
```

## Key rules

| Operation | Contract |
|---|---|
| Decode public keys | Requires the concrete key type and public parameters; rejects private parameters and duplicate labels; skips unknown values. |
| Decode EC2 coordinates | Preserves fixed-width padding. |
| Decode RSA integers | Rejects redundant zero padding. |
| Borrow key bytes | Keep the owner/input alive; metadata collections may still allocate. |
| Compute a thumbprint | Hashes required public parameters only; text curves need resolution and compressed EC2 points need expansion. |
| Supply a SHA-256 backend | Implement `digest(parts, output)` returning `expected<void, status_code>`; consume byte spans once and write 32 bytes. |
| Import a P-256 key | Checks the point, algorithm, and verification permission; the OpenSSL handle retains the algorithm restriction. |
| Validate key use | Checks representation and metadata restrictions, not mathematical validity; absent operations are unrestricted, an empty list permits none. |

## Confirm the presenter's key

- [Encode a confirmation claim](../examples/37_experimental_cwt_confirmation.cpp).
- [Sign and verify a confirmation token, then prove possession](../examples/39_experimental_cwt_confirmation_token.cpp).

Confirmation methods must identify the same key; parsing or authenticating the issuer's token alone does not prove presenter possession. Inline and encrypted key methods cannot coexist; encrypted confirmation and secret-key transport are unsupported.

See [RFC 9679](https://www.rfc-editor.org/rfc/rfc9679.html) for thumbprints and [the custom signing backend example](../examples/24_experimental_cwt_backend.cpp) for connecting a crypto library.
