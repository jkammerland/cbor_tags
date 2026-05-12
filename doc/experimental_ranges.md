# Experimental Range And Segment APIs

> [!WARNING]
> EXPERIMENTAL. The range wrappers, raw encoded views, lazy tag scanner, and
> segmented output helpers are still WIP. Names, constraints, and exact
> borrowing behavior may change before they are treated as stable API.

This document describes the current range-related surface. It intentionally
keeps the experimental pieces together because they share the same core rule:
views and borrowed segments reference caller-owned storage, while copied or
owned forms can outlive their source.

## Includes

Use the normal encoder/decoder headers for core behavior. Include the dedicated
headers only when using the experimental APIs.

```cpp
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_ranges.h>

#include <cbor_tags/cbor_lazy_tags.h> // find_tags
#include <cbor_tags/cbor_raw_views.h> // encoded_*_view types
#include <cbor_tags/cbor_segments.h>  // segmented output helpers
```

## Supported CBOR Buffers

CBOR input/output buffers must use `std::byte` or a one-byte non-`bool`
integral byte type. Common accepted element types are `std::byte`, `char`,
`signed char`, `unsigned char`, `std::uint8_t`, and `std::int8_t` when the
integer aliases are one byte on the target.

Supported input buffers are:

- contiguous ranges such as `std::vector<std::byte>`, `std::array<std::byte, N>`,
  and `std::span<std::byte>`
- bidirectional non-contiguous ranges such as `std::deque<std::byte>` and
  `std::list<std::byte>`

Forward-only non-contiguous buffers are intentionally rejected. Decoder retry
paths need to step back one byte after a failed alternative match, and that is
not safe for forward-only non-contiguous iterators.

## Explicit Range Encoding

Use range wrappers when the source is a view or when the CBOR shape must be
explicit. Plain pair ranges are not encoded as maps automatically.

```cpp
std::vector<std::byte> data;
auto enc = cbor::tags::make_encoder(data);

std::vector<int> values{1, 2, 3};
enc(cbor::tags::as_array_range(values)); // 83 01 02 03

auto odd_pairs = std::views::iota(0, 5)
               | std::views::filter([](int value) { return (value % 2) == 1; })
               | std::views::transform([](int value) {
                     return std::pair{value, value * 10};
                 });

enc(cbor::tags::as_map_range(odd_pairs)); // bf 01 0a 03 18 1e ff
```

Sized ranges encode as definite CBOR arrays or maps. Non-sized ranges encode as
indefinite arrays or maps.

Array ranges may materialize proxy references through the range value type when
that value type is CBOR-encodable. This is needed for sources such as
`std::vector<bool>`. Map ranges are stricter: each entry must be pair-like, and
the key and mapped references must be directly CBOR-encodable.

Nested wrappers are supported only when the wrapper can be iterated in the
cv-qualified context used by the outer range. For example, a mutable outer range
can hold `as_array_range(filter_view)`, but a `const` outer range is rejected
when the wrapped standard `filter_view` is not const-iterable.

## Byte-String Ranges

Use `as_bstr_range(byte_range, chunk_size)` to encode byte-like ranges as CBOR
byte strings.

```cpp
auto bytes = std::views::iota(0, 5)
           | std::views::filter([](int) { return true; })
           | std::views::transform([](int value) {
                 return static_cast<std::byte>(value);
             });

enc(cbor::tags::as_bstr_range(bytes, 2)); // 5f 42 00 01 42 02 03 41 04 ff
```

Sized byte ranges encode as definite byte strings. Non-sized byte ranges encode
as indefinite byte strings in definite chunks. `chunk_size` defaults to `4096`
and must be greater than zero.

`as_bstr_range` is only a byte-string wrapper. It does not add typed-array
semantics or CBOR tag 24 encoded-CBOR semantics.

## View Decode Targets

View decode targets avoid copying and borrow from the original input buffer.
The input buffer must remain alive and must not be reallocated or mutated while
the view is used.

Contiguous input buffers can decode:

- `std::string_view` for text strings
- `std::span<const std::byte>` or `std::basic_string_view<std::byte>` for byte
  strings
- `encoded_item_view`, `encoded_array_view`, and `encoded_map_view` for raw
  encoded CBOR items

Non-contiguous input buffers cannot produce contiguous views. Use the
decoder-provided aliases for those cases:

```cpp
std::deque<std::byte> data;
auto enc = cbor::tags::make_encoder(data);
enc(std::string{"hello"});

auto dec = cbor::tags::make_decoder(data);
decltype(dec)::tstr_view_t text;
dec(text);

std::string copied{text};
```

For raw encoded values from non-contiguous buffers, use:

```cpp
auto dec = cbor::tags::make_decoder(data);

decltype(dec)::raw_encoded_item_view item;
decltype(dec)::raw_encoded_array_view array;
decltype(dec)::raw_encoded_map_view map;
```

Trying to decode `std::string_view`, `std::span<const std::byte>`, or a
span-backed raw encoded view from a non-contiguous buffer fails with
`status_code::contiguous_view_on_non_contiguous_data`.

## Lazy Tag Scanning

`find_tags` scans an existing CBOR buffer and yields matching tags without
materializing unrelated payloads.

```cpp
auto matches = cbor::tags::find_tags<100, 200>(buffer);

for (const auto& match : matches) {
    auto tag = match.tag();
    auto encoded_payload = match.payload_range();

    payload_type payload{};
    auto result = match.decode(payload);
}

if (matches.failed()) {
    auto status = matches.status();
}
```

Runtime filters are also supported:

```cpp
auto even_tags = cbor::tags::find_tags(buffer, [](std::uint64_t tag) {
    return (tag % 2U) == 0U;
});
```

Each match exposes:

- `tag()` for the numeric tag
- `payload_range()` for the encoded payload bytes as a read-only range
- `payload_span()` only when the original buffer is contiguous
- `decode(T&)` and `make_decoder()` for decoding only the payload

The scanner descends into arrays, maps, and tags, and skips non-matching payloads
without decoding them into user objects. Malformed or truncated CBOR stops
iteration and is reported through `status()` and `failed()`.

The returned view borrows from the original buffer. The buffer must outlive the
tag view, matches, payload ranges, payload spans, and payload decoders.

## Segmented Output

`cbor_segments` stores encoded output as a sequence of owned and borrowed byte
segments. It is useful when a large payload already exists and should be emitted
without copying into one contiguous output buffer.

```cpp
std::array<std::byte, 4> payload{
    std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}
};

auto segments = cbor::tags::encode_bstr_segments(std::span<const std::byte>{payload});

// The header is owned by the segment container. The payload is borrowed.
for (const auto& segment : segments) {
    auto bytes = segment.bytes();
}

auto contiguous = cbor::tags::flatten_segments(segments); // copies all bytes
```

The encoder can also write directly into a segment container:

```cpp
cbor::tags::cbor_segments segments;
auto enc = cbor::tags::make_encoder(segments);
enc(cbor::tags::as_array{2}, 1, 2);

auto bytes = segments.flatten();
```

Helpers are available for common byte-string shapes:

```cpp
cbor::tags::cbor_segments segments;
auto payload_span = std::span<const std::byte>{payload};

cbor::tags::encode_bstr_segments_into(segments, payload_span);
cbor::tags::encode_indefinite_bstr_segments_into(segments, payload_span, 4096);
cbor::tags::encode_tagged_bstr_segments_into(segments, std::uint64_t{24}, payload_span);
```

Owned segment boundaries are not semantic. The default `byte_segments` storage
may coalesce adjacent owned writes. Borrowed segment boundaries are preserved
because they carry the caller-owned lifetime contract. Compare output with
`flatten_segments(...)` or `flatten_to(...)` when byte-for-byte equality matters.

## Raw Encoded Views As Segments

Raw encoded views can be converted to segments. The default path is safe and
owned:

```cpp
auto dec = cbor::tags::make_decoder(buffer);
decltype(dec)::raw_encoded_array_view array;
dec(array);

auto owned_segments = cbor::tags::to_segments(array);   // copies bytes
auto also_owned     = cbor::tags::as_segments(array);   // same behavior
auto explicit_copy  = cbor::tags::copy_segments(array); // same ownership
```

Use `borrow_segments` only when the raw encoded view is span-backed and the
source buffer is an lvalue with a lifetime that will outlive the segments. This
is available for contiguous raw views:

```cpp
std::vector<std::byte> buffer{std::byte{0x82}, std::byte{0x01}, std::byte{0x02}};
auto dec = cbor::tags::make_decoder(buffer);
decltype(dec)::raw_encoded_array_view array;
dec(array);

auto borrowed = cbor::tags::borrow_segments(array); // no payload copy
```

`borrow_segments` is deleted for rvalue views. Non-contiguous raw encoded views
must use `copy_segments`.

The older `encode_encoded_segments(view)` name is a compatibility wrapper around
`borrow_segments(view)`. Prefer `borrow_segments` or `copy_segments` in new code.

## Encoded Items Inside Byte Strings

Validated encoded item segments can be wrapped as a CBOR byte string without
flattening first.

```cpp
std::vector<int> values{1, 2};
auto encoded = cbor::tags::encode_item_segments(values);

std::vector<std::byte> output;
auto enc = cbor::tags::make_encoder(output);
if (encoded) {
    enc(cbor::tags::as_bstr(*encoded));
}
```

`as_bstr(encoded_item_segments)` only accepts lvalues. This prevents a borrowed
byte string wrapper from outliving the validated item.

## Custom Segment Storage

The default type is:

```cpp
using cbor_segments = cbor::tags::basic_byte_segments<
    cbor::tags::default_byte_segment_storage<>
>;
```

Custom storage can tune the segment object and backing container, for example to
use inline capacity or a `std::pmr::vector`. The storage type must provide:

- `segment_type`
- `container_type`
- `make_container()`
- `make_owned(std::span<const std::byte>)`
- `make_borrowed(std::span<const std::byte>)`

Optional hooks are:

- `try_append_owned(container, bytes) -> bool`
- `append_owned(container, bytes) -> void`
- `reserve_owned_bytes(count)`

Custom `basic_byte_segments<Storage>` backends are currently scoped to direct
encoder output and `_into` helpers. Validated item helpers such as
`encode_item_segments(...)` currently return the default `encoded_item_segments`
using the default `cbor_segments` backend.

## Current Limitations

- These APIs are experimental and may change.
- Lazy array/map element decode views are not part of this pass.
- `payload_span()` exists only for contiguous input buffers.
- `borrow_segments` is intentionally explicit and limited to lvalue,
  span-backed raw encoded views.
- Flattening segmented output always copies.
- A single contiguous output buffer cannot be true zero-copy when payload bytes
  live elsewhere.
- Text-string view decode is byte-preserving; UTF-8 validation is not currently
  performed by these view APIs.
