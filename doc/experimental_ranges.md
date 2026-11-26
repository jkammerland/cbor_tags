# Experimental Range And Segment APIs

> [!WARNING]
> EXPERIMENTAL. These APIs are still a WIP. Names and exact borrowing behavior may
> change before they are treated as stable API.

This page covers a few related APIs that are easy to confuse:

- range wrappers for encoding existing C++ ranges,
- borrowed views for decoding without copying payload bytes,
- encoded CBOR views for forwarding already-encoded items,
- lazy tag scanning,
- segmented output for avoiding large payload copies.

They are related because they all help avoid unnecessary temporary containers or
copies. They are not one feature.

For small, compiled programs with CDDL and executable checks, start with the
[numbered experimental examples](../examples/README.md#learning-order).
They introduce range encoding, borrowed decode views, segmented output and encoded-view
forwarding separately.

Use normal containers for ordinary array, map and string encoding. Range
wrappers choose the CBOR type for an existing C++ range.

The examples assume:

```cpp
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_ranges.h>

#include <cbor_tags/cbor_lazy_tags.h>
#include <cbor_tags/cbor_segments.h>

using namespace cbor::tags;
```

## Range Wrappers Encode Now

A range wrapper is an instruction to the encoder. It is not a CBOR buffer, and
it is not a lazy encoded object.

When you pass a wrapper to the encoder, the encoder immediately iterates the C++
range and writes CBOR bytes to the output buffer.

```cpp
std::vector<std::byte> out;
auto enc = make_encoder(out);

std::vector<int> values{1, 2, 3};

if (!enc(as_array_range(values))) return 1; // writes 83 01 02 03 into out here
```

The wrapper exists because some ranges are ambiguous:

```cpp
std::vector<std::pair<int, int>> pairs{{1, 10}, {2, 20}};

if (!enc(as_array_range(pairs))) return 1; // array containing pair-like values
if (!enc(as_map_range(pairs))) return 1;   // map: 1 => 10, 2 => 20
```

Byte ranges have the same problem:

```cpp
std::vector<unsigned char> payload{1, 2, 3};

if (!enc(as_array_range(payload))) return 1; // array of integer values
if (!enc(as_bstr_range(payload))) return 1;  // CBOR byte string
```

So the wrappers make the CBOR major type explicit:

- `as_array_range(r)` means encode `r` as a CBOR array.
- `as_map_range(r)` means encode a range of pairs as a CBOR map.
- `as_tstr_range(r)` means encode a character range as a CBOR text string.
- `as_bstr_range(r)` means encode a byte-like range as a CBOR byte string.

## Use Case: Avoid A Temporary Container

The common use case is a C++ view or generated range. Without a wrapper, you
would have to build a temporary `std::vector`, `std::map`, or `std::string`
first.

```cpp
std::vector<int> values{1, 2, 3, 4, 5, 6};

auto evens = values | std::views::filter([](int value) {
    return (value % 2) == 0;
});

if (!enc(as_array_range(evens))) return 1; // encodes 2, 4, 6 without building a new vector
```

A generated map can be encoded without building a `std::map`:

```cpp
auto pairs = std::views::iota(1, 4)
           | std::views::transform([](int value) {
                 return std::pair{value, value * 10};
             });

if (!enc(as_map_range(pairs))) return 1; // a3 01 0a 02 14 03 18 1e
```

A transformed byte or character stream can be encoded without first building an
owning byte buffer or string:

```cpp
std::array<unsigned char, 5> source{0, 1, 2, 3, 4};

auto bytes = source | std::views::transform([](unsigned char value) {
    return static_cast<std::byte>(value);
});

if (!enc(as_bstr_range(bytes))) return 1; // 45 00 01 02 03 04
```

`as_tstr_range` accepts character ranges. It does not validate UTF-8; it only
selects CBOR text string encoding.

```cpp
std::string name{"Ada"};

auto upper = name | std::views::transform([](char value) {
    return static_cast<char>(value >= 'a' && value <= 'z' ? value - 'a' + 'A' : value);
});

if (!enc(as_tstr_range(upper))) return 1; // 63 41 44 41
```

## Sized And Non-Sized Ranges

CBOR arrays and maps can be definite length or indefinite length.

If the C++ range has a cheap `size()`, the wrapper emits a definite CBOR array
or map:

```cpp
std::array<int, 3> values{1, 2, 3};

if (!enc(as_array_range(values))) return 1; // 83 01 02 03
```

If the C++ range does not have a known size, the wrapper emits an indefinite
array or map:

```cpp
std::vector<int> values{1, 2, 3, 4, 5, 6};

auto evens = values | std::views::filter([](int value) {
    return (value % 2) == 0;
});

if (!enc(as_array_range(evens))) return 1; // 9f ... ff
```

Text and byte strings behave similarly, but CBOR requires indefinite strings to
be split into definite chunks. For non-sized text or byte ranges, the wrapper
collects up to `chunk_size` bytes at a time, writes that chunk, and then
continues.

```cpp
std::string name{"Ada"};

auto every_char = name | std::views::filter([](char) {
    return true;
});

if (!enc(as_tstr_range(every_char, 2))) return 1; // 7f 62 41 64 61 61 ff
```

`chunk_size` defaults to `4096` and must be greater than zero.

Explicit range wrappers can be combined with `bounded_size` when the protocol
has a known element or byte limit and the wrapped range is a sized range. The
encoder checks `size()` before writing any output. The bound belongs to
`bounded_size`; `chunk_size` on `as_bstr_range` and `as_tstr_range` only controls
indefinite string chunking.

```cpp
namespace ct = cbor::tags;

std::vector<int> values{1, 2, 3};

if (!enc(ct::as_bounded_size<0, 3>(ct::as_array_range(values)))) return 1;
// CDDL: [0*3 int]
```

Limits selected at runtime use the same sized-range path:

```cpp
std::size_t max_values = configured_limit();
if (!enc(ct::as_bounded_size(ct::as_array_range(values), 0, max_values))) return 1;
```

The runtime form checks `size()` once before encoding and does not describe a
type-based CDDL constraint. Use the compile-time form when the bound belongs in
the schema.

Bounded encoding of a non-sized range is intentionally unsupported because
checking the bound before writing would require a separate counting traversal.
The unbounded explicit wrapper remains one-pass:

```cpp
namespace ct = cbor::tags;

auto evens = values | std::views::filter([](int value) {
    return (value % 2) == 0;
});

if (!enc(ct::as_array_range(evens))) return 1;
```

```cpp
namespace ct = cbor::tags;

std::array<unsigned char, 5> source{0, 1, 2, 3, 4};

auto odd_bytes = source | std::views::filter([](unsigned char value) {
    return (value % 2U) == 1U;
}) | std::views::transform([](unsigned char value) {
    return static_cast<std::byte>(value);
});

if (!enc(ct::as_bstr_range(odd_bytes, 2))) return 1; // 5f 42 01 03 ff
```

Use a sized byte range when bounded encoding is required. CDDL generation can
still describe a bounded explicit range wrapper independently of whether that
particular C++ range type is encodable.

Range wrappers are encode/CDDL helpers. They are not decode targets; decode into
an owning container, a string, or one of the borrowed decode view types described
below.

## Range Wrapper Lifetimes

The wrapper stores a C++ view of the range. If you encode immediately, this is
usually invisible:

```cpp
if (!enc(as_array_range(values))) return 1;
```

If you store the wrapper and encode it later, the usual C++ range lifetime rules
matter.

```cpp
auto wrapped = as_array_range(values);

// values must still exist when wrapped is encoded.
if (!enc(wrapped)) return 1;
```

Temporaries are accepted only when `std::views::all` can store them safely for
the current standard library implementation.

```cpp
auto wrapped = as_array_range(std::vector<int>{1, 2, 3});
if (!enc(wrapped)) return 1;
```

## Borrowed Decode Views

Borrowed decode views are a different feature from range wrappers.

A borrowed decode view means: decode the CBOR string header and payload
boundaries, but return a view that points into the input buffer instead of
copying the payload into a new `std::string` or `std::vector`.

```cpp
std::vector<std::byte> data;
auto enc = make_encoder(data);
if (!enc(std::string{"hello"})) return 1;

auto dec = make_decoder(data);
std::string_view text;
if (!dec(text)) return 1;
```

`text` points into `data`. Keep `data` alive and do not invalidate it while
using `text`.

Use `std::span<const std::byte>` for a contiguous borrowed byte-string payload:

```cpp
std::vector<std::byte> data;
auto enc = make_encoder(data);
if (!enc(std::vector<std::byte>{std::byte{1}, std::byte{2}})) return 1;

auto dec = make_decoder(data);
std::span<const std::byte> bytes;
if (!dec(bytes)) return 1;
```

`bytes` borrows from `data` with the same lifetime requirements as `text`.
Decoding into `std::basic_string_view<std::byte, Traits>` is also supported when
`Traits` implements the character-traits requirements. The standard library is
not required to provide `std::char_traits<std::byte>`; use explicit traits for
nonstandard string character types, or prefer the span API above.

Borrowed text and byte-string views decode definite strings only. If the input
may contain indefinite/chunked text or byte strings, decode into an owning
`std::string` or byte container instead.

For non-contiguous input buffers, use the decoder-provided aliases:

```cpp
std::deque<std::byte> data;
auto enc = make_encoder(data);
if (!enc(std::string{"hello"})) return 1;

auto dec = make_decoder(data);
decltype(dec)::tstr_view_t text;
if (!dec(text)) return 1;
```

## Encoded Item Views

An encoded item view refers to a complete CBOR item, including its header.

Use them when you want to copy, forward, or embed a complete CBOR item without
decoding it into a C++ value and encoding it again.

```cpp
std::vector<std::byte> data;
auto enc = make_encoder(data);
if (!enc(std::vector<int>{1, 2, 3})) return 1;

auto dec = make_decoder(data);
decltype(dec)::encoded_array_view encoded;
if (!dec(encoded)) return 1;

std::vector<std::byte> forwarded;
auto out = make_encoder(forwarded);
if (!out(encoded)) return 1; // re-emits the same encoded array bytes
```

The encoded view borrows from the decoder input. For contiguous input, `encoded.span()`
is available. For all supported input buffers, `encoded.bytes()` can be iterated.

## Lazy Tag Scanning

Lazy tag scanning is the part that is actually lazy about decoding payloads.

`find_tags` scans nested CBOR structure and returns matches for the requested
tag numbers. It does not decode unrelated payloads into C++ objects. You
decode a matched payload only when you ask for it.

```cpp
auto matches = find_tags<100, 200>(buffer);

for (const auto& match : matches) {
    if (match.tag() != 100) {
        continue;
    }

    MyPayload payload{};
    if (!match.decode(payload)) return 1;
}

if (matches.failed()) {
    auto status = matches.status();
}
```

Scanning is incremental, not whole-buffer validation. `failed()` reports only
work the iterator has already performed, so advance through `matches.end()`
before treating its status as the result for the complete input segment. If a
caller stops after the first match, malformed or deeply nested trailing data
has not been scanned yet. The current scanner permits at most 256
simultaneously open array, map, or tag frames. It returns `status_code::size_limit_exceeded`
when scanning or locating a matched payload boundary would require another
frame; that payload scan shares the same frame budget. This is a
scanner-specific terminal limit, not a core decoder rollback or resumability
guarantee.

Runtime filters are also supported:

```cpp
auto even_tags = find_tags(buffer, [](std::uint64_t tag) {
    return (tag % 2U) == 0U;
});
```

Use `payload_range()` for encoded payload bytes. Use `payload_span()` only when
the original buffer is contiguous.

## Segmented Output

Normal encoder output is one byte container, such as `std::vector<std::byte>`.

`byte_segments` is different. It stores output as a list of byte segments. Small
headers are owned by the segment container. Large payload spans can be borrowed
instead of copied.

This is useful when a large byte payload already exists and you do not want to
copy it into one contiguous output buffer just to write it somewhere else.

```cpp
std::array<std::byte, 4> payload{
    std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}
};

auto segments = encode_bstr_segments(std::span<const std::byte>{payload});

for (const auto& segment : segments) {
    auto bytes = segment.bytes();
}

auto contiguous = flatten_segments(segments); // copies all segment bytes
```

You can also use segments as the encoder output backend:

```cpp
byte_segments segments;
auto enc = make_encoder(segments);
if (!enc(as_array{2}, 1, 2)) return 1;

auto contiguous = segments.flatten();
```

Append helpers let you build segmented output incrementally:

```cpp
byte_segments segments;
auto payload_span = std::span<const std::byte>{payload};

encode_bstr_segments_into(segments, payload_span);
encode_indefinite_bstr_segments_into(segments, payload_span, 4096);
encode_tagged_bstr_segments_into(segments, std::uint64_t{24}, payload_span);
```

Sources passed to `append_owned()` must remain valid throughout the call.
Appending may reallocate owned storage and invalidate views into the
destination's bytes; self-append is not guaranteed. To duplicate an owned
segment's bytes, first copy them into independent storage:

```cpp
auto segment = byte_segment::owned(payload_span);
const auto bytes = segment.bytes();
std::vector<std::byte> copy(bytes.begin(), bytes.end());
segment.append_owned(copy);
```

Flattening copies the bytes. Keep borrowed payload storage alive and unchanged
until the segments are written or flattened.

## Convert Encoded Views To Segments

Use `copy_segments` when the segment output should own a copy of the encoded bytes.

```cpp
auto dec = make_decoder(buffer);
decltype(dec)::encoded_array_view encoded;
if (!dec(encoded)) return 1;

auto owned = copy_segments(encoded);
```

Use `borrow_segments` only when the encoded view came from contiguous lvalue input
that will outlive the segments.

```cpp
std::vector<std::byte> buffer{std::byte{0x82}, std::byte{0x01}, std::byte{0x02}};
auto dec = make_decoder(buffer);
decltype(dec)::encoded_array_view encoded;
if (!dec(encoded)) return 1;

auto borrowed = borrow_segments(encoded);
```

## Wrap Encoded Items In Bstr

Validated encoded item segments can be wrapped as a CBOR byte string without
flattening first.

```cpp
std::vector<int> values{1, 2};
auto encoded = encode_item_segments(values);

std::vector<std::byte> output;
auto enc = make_encoder(output);

if (!encoded || !enc(as_bstr(*encoded))) return 1;
```
