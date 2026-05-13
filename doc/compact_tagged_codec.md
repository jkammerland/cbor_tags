# Compact Tagged Codec

`compact_tagged_codec` is an opt-in codec for cases where both sides already
share the C++ schema and want a smaller payload than normal self-describing
CBOR fields.

The outer value is still CBOR:

```text
#6.<tag>(bstr)
```

The byte-string payload is a compact schema-bound format. It is not standalone
CBOR, and a generic CBOR decoder can only see the tag and opaque byte string.

Use the normal CBOR codec when data should remain self-describing. Use this
codec when the tag identifies the schema and compactness matters more than
generic field inspection.

## Usage

```cpp
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/compact_tagged.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct Message {
    static constexpr std::uint64_t cbor_tag = 1001;

    std::uint16_t id{};
    std::string   label;
};

std::vector<std::byte> out;

using namespace cbor::tags;
using namespace cbor::tags::ext::compact;

Message message{.id = 7, .label = "ready"};
auto    enc     = make_encoder<compact_tagged_codec>(out);
auto    encoded = enc(as_compact(message));

Message decoded{};
auto    dec        = make_decoder<compact_tagged_codec>(out);
auto    decoded_ok = dec(as_compact(decoded));
```

`as_compact(value)` requires the type to provide a CBOR tag, using the same tag
mechanisms as normal tagged values. If the tag should be supplied at the call
site, pass it explicitly:

```cpp
auto result = enc(as_compact(static_tag<1001>{}, message));
```

## Payload Rules

- Integral values, enums, and floats are encoded in fixed-width little-endian
  form.
- `bool` uses one byte: `0` or `1`.
- Text strings, byte strings, ranges, and maps use a compact varuint length
  followed by their elements or bytes.
- `std::optional<T>` uses a one-byte presence marker followed by `T` when set.
- `std::variant<...>` stores the selected alternative index followed by that
  alternative.
- Aggregates and tuple-like values are encoded as fields in schema order, with
  no field names or CBOR array wrapper inside the payload.

This means the decoded C++ type must match the schema used for encoding. The
codec validates the outer tag, the byte-string wrapper, malformed compact
payloads, trailing payload bytes, and incomplete input, but it does not carry
field names or generic type descriptors inside the payload.

## Borrowed Views

Decoded `std::string_view`, `std::span<const std::byte>`, and nested structures
containing those views borrow from the byte-string payload. They require a
contiguous input buffer. When the decoder input is non-contiguous, decode into
owning types such as `std::string` and `std::vector<std::byte>` instead.

## Name

`compact_tagged_codec` is deliberately explicit: the public CBOR shape is a tag
whose content is a byte string containing compact payload bytes.

`plain_codec` describes the payload style, but hides the fact that the wire
value is still tagged CBOR. `zppbits_codec` would imply wire compatibility with
zpp_bits, which this format does not promise. If a shorter public spelling is
needed later, prefer a name that still keeps the compact/tagged distinction
clear.
