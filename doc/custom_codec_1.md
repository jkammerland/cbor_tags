# Custom Codec 1

`custom_codec_1` is an opt-in codec for cases where both sides already
share the C++ schema and want a smaller payload than normal self-describing
CBOR fields.

The outer value is still CBOR:

```text
#6.<tag>(bstr)
```

The byte-string payload is a compact schema-bound format. It is not standalone
CBOR, and a generic CBOR decoder can only see the tag and opaque definite-length
byte string.

Use the normal CBOR codec when data should remain self-describing. Use this
codec when the tag identifies the schema and compactness matters more than
generic field inspection.

## Usage

```cpp
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_lazy_tags.h>
#include <cbor_tags/extensions/custom_codec_1.h>

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
auto    enc     = make_encoder<custom_codec_1>(out);
auto    encoded = enc(as_compact(message));

Message decoded{};
auto    dec        = make_decoder<custom_codec_1>(out);
auto    decoded_ok = dec(as_compact(decoded));
```

`as_compact(value)` requires the type to provide a CBOR tag, using the same tag
mechanisms as normal tagged values. If the tag should be supplied at the call
site, pass it explicitly:

```cpp
auto result = enc(as_compact(static_tag<1001>{}, message));
```

## Lazy Tag Payloads

`find_tags` matches the outer CBOR tag and exposes only the tag payload. For
compact tagged values, that payload is the definite-length byte string, not the
whole `#6.<tag>(bstr)` envelope. Decode it with `as_compact_payload`.

```cpp
auto matches = find_tags<1001>(out);
auto it      = matches.begin();

Message decoded_from_match{};
auto    payload_ref = as_compact_payload(decoded_from_match);
auto    ok          = it->decode<custom_codec_1>(payload_ref);
```

The payload decoder object also accepts the wrapper directly:

```cpp
Message decoded_from_payload{};
auto    payload_decoder = it->make_decoder<custom_codec_1>();
auto    ok = payload_decoder(as_compact_payload(decoded_from_payload));
```

Use `as_compact(...)` for full buffers that still contain the outer tag. Use
`as_compact_payload(...)` only when the decoder starts at the tag payload bstr,
as lazy tag matches do.

## Payload Rules

- Integral values, enums, and floats are encoded in fixed-width little-endian
  form.
- `bool` uses one byte: `0` or `1`.
- Text strings, byte strings, variable-size ranges, and maps use a compact
  varuint length followed by their elements or bytes.
- `std::array` stores its elements directly; its length is part of the C++
  schema, not the payload.
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

Borrowed views point into the input buffer. Destroying, mutating, or reallocating
that buffer invalidates the decoded views.

## Segmented Encoding Direction

The current `custom_codec_1` encoder has to know the byte-string payload length
before it can write the payload header. That is why it measures the payload
before writing it.

`cbor_segments` is the right direction for removing that repeated walk without
forcing one contiguous temporary buffer. The payload can be encoded once into
replayable segments, then the outer tag and byte-string header can be
prepended after the payload size is known.

This is the intended implementation shape;
`encode_custom_codec_1_payload_segments` is not currently a public API.

```cpp
#include <cbor_tags/cbor_segments.h>

template <typename T>
cbor_segments encode_custom_codec_1_segments(std::uint64_t tag, const T& value) {
    cbor_segments payload = encode_custom_codec_1_payload_segments(value);

    cbor_segments out;
    out.reserve_segments(payload.size() + 2);

    out.append_owned(detail::encode_cbor_major_argument_header(tag, std::byte{0xc0}).span());
    out.append_owned(detail::encode_cbor_major_argument_header(payload.total_size(), std::byte{0x40}).span());

    for (const auto& segment : payload) {
        if (segment.is_borrowed()) {
            out.append_borrowed(segment.bytes());
        } else {
            out.append_owned(segment.bytes());
        }
    }

    return out;
}
```

The caller can then replay the segments directly to a vectored I/O interface
without flattening:

```cpp
std::vector<iovec> iovecs;
iovecs.reserve(segments.size());

for (const auto& segment : segments) {
    auto bytes = segment.bytes();
    iovecs.push_back(iovec{
        .iov_base = const_cast<std::byte*>(bytes.data()),
        .iov_len  = bytes.size(),
    });
}
```

Small generated headers remain owned by the segment container. Large payload
ranges can be borrowed when their source storage outlives the write operation.

## Known Limitations

- `custom_codec_1` payload encoding measures the payload and then writes it. Do not pass
  one-shot or stateful input ranges to `as_compact(...)`; materialize them into a
  stable container first.
- Malformed compact payloads can currently declare very large variable-size
  container lengths before the decoder proves the payload is incomplete. Decode
  compact tagged data only from inputs that are already bounded by your
  transport, file-size limit, or application framing.
- Additional opt-in codecs passed beside `custom_codec_1` compose at the outer
  CBOR level. Payload fields use this codec's schema-bound payload
  rules, not the normal extension dispatch path.

## Name

`custom_codec_1` is intentionally neutral. The important API contract is the
wire shape and schema-bound payload rules above, not a promise that integers,
floats, or other scalar values are minimized.

`plain_codec` would hide that the wire value is still tagged CBOR. `zppbits_codec`
would imply wire compatibility with zpp_bits, which this format does not
promise.
