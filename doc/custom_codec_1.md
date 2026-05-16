# Custom Codec 1

`custom_codec_1` is an opt-in codec for cases where both sides already
share the C++ schema and do not need normal self-describing CBOR fields inside
the tagged payload.

The outer value is still CBOR:

```text
#6.<tag>(bstr)
```

The byte-string payload is a schema-bound format. It is not standalone CBOR, and
a generic CBOR decoder can only see the tag and opaque definite-length byte
string.

Use the normal CBOR codec when data should remain self-describing. Use this
codec when the tag identifies the schema and generic field inspection is not
required.

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
namespace cc1 = cbor::tags::ext::custom_codec_1;

Message message{.id = 7, .label = "ready"};
auto    enc     = make_encoder<cc1::custom_codec_1>(out);
auto    encoded = enc(cc1::as_custom_codec_1(message));

Message decoded{};
auto    dec        = make_decoder<cc1::custom_codec_1>(out);
auto    decoded_ok = dec(cc1::as_custom_codec_1(decoded));
```

`as_custom_codec_1(value)` requires the type to provide a CBOR tag, using the same tag
mechanisms as normal tagged values. If the tag should be supplied at the call
site, pass it explicitly:

```cpp
auto result = enc(cc1::as_custom_codec_1(static_tag<1001>{}, message));
```

## Lazy Tag Payloads

`find_tags` matches the outer CBOR tag and exposes only the tag payload. For
`custom_codec_1` values, that payload is the definite-length byte string, not the
whole `#6.<tag>(bstr)` envelope. Decode it with `as_custom_codec_1_payload`.

```cpp
auto matches = find_tags<1001>(out);
auto it      = matches.begin();

if (it != matches.end()) {
    Message decoded_from_match{};
    auto    payload_ref = cc1::as_custom_codec_1_payload(decoded_from_match);
    auto    ok          = it->decode<cc1::custom_codec_1>(payload_ref);
}

if (matches.failed()) {
    auto status = matches.status();
}
```

The payload decoder object also accepts the wrapper directly:

```cpp
if (it != matches.end()) {
    Message decoded_from_payload{};
    auto    payload_decoder = it->make_decoder<cc1::custom_codec_1>();
    auto    ok = payload_decoder(cc1::as_custom_codec_1_payload(decoded_from_payload));
}
```

Use `as_custom_codec_1(...)` for full buffers that still contain the outer tag. Use
`as_custom_codec_1_payload(...)` only when the decoder starts at the tag payload bstr,
as lazy tag matches do.

## Payload Rules

- Integral values, enums, and floats are encoded in fixed-width little-endian
  form.
- `bool` uses one byte: `0` or `1`.
- Text strings, byte strings, variable-size ranges, and maps use a varuint
  length followed by their elements or bytes.
- Contiguous ranges and fixed arrays of unsigned integers and floating-point
  values are bulk-copied on little-endian hosts. `bool` is intentionally
  excluded so decode still rejects bytes other than `0` and `1`. The wire shape
  is the same as the element-by-element encoding.
- Text strings include `std::basic_string` and `std::basic_string_view` with
  char-like value types, including `std::pmr::string`.
- `std::array` stores its elements directly; its length is part of the C++
  schema, not the payload.
- `std::optional<T>` uses a one-byte presence marker followed by `T` when set.
- `std::variant<...>` stores the selected alternative index followed by that
  alternative.
- Aggregates and tuple-like values are encoded as fields in schema order, with
  no field names or CBOR array wrapper inside the payload.

This means the decoded C++ type must match the schema used for encoding. The
codec validates the outer tag, the byte-string wrapper, trailing payload bytes,
incomplete input, and malformed scalar/schema markers, but it does not carry
field names or generic type descriptors inside the payload.

## Borrowed Views

Decoded `std::string_view`, `std::span<const std::byte>`, and nested structures
containing those views borrow from the byte-string payload. They require a
contiguous input buffer. When the decoder input is non-contiguous, decode into
owning types such as `std::string` and `std::vector<std::byte>` instead.

Borrowed views point into the input buffer. Destroying, mutating, or reallocating
that buffer invalidates the decoded views.

## Segmented Encoding

`custom_codec_1` has to know the byte-string payload length before it can write
the payload header. The encoder now writes the payload once into scratch storage,
computes that size, then writes the outer tag and byte-string headers before
replaying the payload.

The normal user-facing API stays the same:

```cpp
std::vector<std::byte> out;
auto enc = make_encoder<cc1::custom_codec_1>(out);

enc(cc1::as_custom_codec_1(static_tag<1001>{}, message));
```

Using `cbor_segments` as the output buffer lets callers replay the encoded bytes
to vectored I/O without first flattening them into one contiguous buffer:

```cpp
#include <cbor_tags/cbor_segments.h>

#include <sys/uio.h> // POSIX iovec

cbor_segments segments;
auto enc = make_encoder<cc1::custom_codec_1>(segments);

enc(cc1::as_custom_codec_1(static_tag<1001>{}, message));

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

The wire format is unchanged. For input ranges that the codec already accepts,
encoding now performs one payload pass instead of walking once for sizing and
again for writing.

The normal encoder keeps segment storage owned by the output, so it is safe to
use after the source object goes out of scope. When the source object is known
to outlive the I/O operation, use `encode_borrowed_segments(...)` to let
contiguous payload fields become borrowed segments:

```cpp
std::vector<float> samples = load_samples();

auto segments = cc1::encode_borrowed_segments(static_tag<1001>{}, samples);

// segments may contain borrowed spans into samples; keep samples alive until
// the write operation has consumed the segments.
```

## Known Limitations

- Malformed payloads can currently declare very large variable-size
  container lengths before the decoder proves the payload is incomplete. Decode
  `custom_codec_1` data only from inputs that are already bounded by your
  transport, file-size limit, or application framing.
- `as_custom_codec_1(...)` stores a reference and encoding observes the value
  through a `const` view. Input ranges that can only be iterated through a
  non-`const` `begin()` still need to be materialized or wrapped before
  encoding.
- Encoding needs scratch storage for the payload before the outer byte-string
  header can be written. Appendable allocator-aware output buffers use the same
  allocator for that scratch storage when possible; fixed-size and custom
  segment outputs may still allocate through the library's default segment
  storage.
- Decoding from non-contiguous CBOR input copies the byte-string payload into a
  temporary `std::vector<std::byte>` before payload decoding. That scratch
  allocation is bounded by the outer byte-string payload size, but it is not
  allocated from the target object's PMR resource.
- Leading `dynamic_tag` fields are tag metadata and are not stored again inside
  payload fields. Decoding preserves them when the destination object already
  carries the expected tag, but newly materialized range or map elements cannot
  infer per-element dynamic tag values from the payload.
- `std::variant` alternatives do not currently receive parent PMR allocator
  context. This matches the main decoder limitation; avoid PMR alternatives
  inside variants when allocator containment matters, or decode that branch
  explicitly.
- Non-default-constructible `std::variant` alternatives can be decoded only when
  the destination variant is already seeded with the same selected index, giving
  the codec an existing value to copy and update. Duplicate alternative types
  are supported because decode emplaces by stored index.
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
