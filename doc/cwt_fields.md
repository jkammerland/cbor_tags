# Experimental CWT fields

Send an issuer and an optional role; receive only the issuer as a borrowed string ([example 30](../examples/30_experimental_cwt_fields.cpp)).

```cpp
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
```

## Field rules

| Choice | Behavior |
|---|---|
| Plain destination | Its label must be present. |
| Optional destination | Missing labels clear it after successful decoding; a present value decodes the underlying type. |
| Unknown label | Its value is skipped and omitted on re-encoding. |
| Duplicate label | Rejected, including unknown input labels; duplicate schema labels fail compilation. |
| Nested field groups | Flatten into one map. |
| Decode failure | Terminal; the reader and already-decoded fields are not rolled back. |
| Borrowed text | Keep the input buffer alive and unchanged. |

The field group references its variables; keep them alive too. Owning destinations support noncontiguous input.

Values use the selected codecs and ordinary encode/decode customizations; see [custom headers](cwt_headers.md) for a complete application type.
