# Experimental Range And Segment APIs

> [!WARNING]
> EXPERIMENTAL. These APIs are still WIP. Names and exact borrowing behavior may
> change before they are treated as stable API.

This page covers a few related APIs that are easy to confuse:

- range wrappers for encoding existing C++ ranges,
- borrowed views for decoding without copying payload bytes,
- raw encoded CBOR views for forwarding already-encoded items,
- lazy tag scanning,
- segmented output for avoiding large payload copies.

They are related because they all help avoid unnecessary temporary containers or
copies. They are not one feature.

If you only want to encode normal C++ containers, you usually do not need this
page.

```cpp
std::vector<std::byte> out;
auto enc = make_encoder(out);

enc(std::vector<int>{1, 2, 3});              // CBOR array
enc(std::map<int, int>{{1, 10}, {2, 20}});   // CBOR map
enc(std::string{"Ada"});                    // CBOR text string
```

The range wrappers are for cases where a plain C++ range does not say enough
about the CBOR type you want.

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

enc(as_array_range(values)); // writes 83 01 02 03 into out here
```

The wrapper exists because some ranges are ambiguous:

```cpp
std::vector<std::pair<int, int>> pairs{{1, 10}, {2, 20}};

enc(as_array_range(pairs)); // array containing pair-like values
enc(as_map_range(pairs));   // map: 1 => 10, 2 => 20
```

Byte ranges have the same problem:

```cpp
std::vector<unsigned char> payload{1, 2, 3};

enc(as_array_range(payload)); // array of integer values
enc(as_bstr_range(payload));  // CBOR byte string
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

enc(as_array_range(evens)); // encodes 2, 4, 6 without building a new vector
```

A generated map can be encoded without building a `std::map`:

```cpp
auto pairs = std::views::iota(1, 4)
           | std::views::transform([](int value) {
                 return std::pair{value, value * 10};
             });

enc(as_map_range(pairs)); // a3 01 0a 02 14 03 18 1e
```

A transformed byte or character stream can be encoded without first building an
owning byte buffer or string:

```cpp
std::array<unsigned char, 5> source{0, 1, 2, 3, 4};

auto bytes = source | std::views::transform([](unsigned char value) {
    return static_cast<std::byte>(value);
});

enc(as_bstr_range(bytes)); // 45 00 01 02 03 04
```

`as_tstr_range` accepts character ranges. It does not validate UTF-8; it only
selects CBOR text string encoding.

```cpp
std::string name{"Ada"};

auto upper = name | std::views::transform([](char value) {
    return static_cast<char>(value >= 'a' && value <= 'z' ? value - 'a' + 'A' : value);
});

enc(as_tstr_range(upper)); // 63 41 44 41
```

## Sized And Non-Sized Ranges

CBOR arrays and maps can be definite length or indefinite length.

If the C++ range has a cheap `size()`, the wrapper emits a definite CBOR array
or map:

```cpp
std::array<int, 3> values{1, 2, 3};

enc(as_array_range(values)); // 83 01 02 03
```

If the C++ range does not have a known size, the wrapper emits an indefinite
array or map:

```cpp
std::vector<int> values{1, 2, 3, 4, 5, 6};

auto evens = values | std::views::filter([](int value) {
    return (value % 2) == 0;
});

enc(as_array_range(evens)); // 9f ... ff
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

enc(as_tstr_range(every_char, 2)); // 7f 62 41 64 61 61 ff
```

`chunk_size` defaults to `4096` and must be greater than zero.

```cpp
std::array<unsigned char, 5> source{0, 1, 2, 3, 4};

auto odd_bytes = source | std::views::filter([](unsigned char value) {
    return (value % 2U) == 1U;
}) | std::views::transform([](unsigned char value) {
    return static_cast<std::byte>(value);
});

enc(as_bstr_range(odd_bytes, 2)); // 5f 42 01 03 ff
```

## Range Wrapper Lifetimes

The wrapper stores a C++ view of the range. If you encode immediately, this is
usually invisible:

```cpp
enc(as_array_range(values));
```

If you store the wrapper and encode it later, the usual C++ range lifetime rules
matter.

```cpp
auto wrapped = as_array_range(values);

// values must still exist when wrapped is encoded.
enc(wrapped);
```

Temporaries are accepted only when `std::views::all` can store them safely for
the current standard library implementation.

```cpp
auto wrapped = as_array_range(std::vector<int>{1, 2, 3});
enc(wrapped);
```

## Borrowed Decode Views

Borrowed decode views are a different feature from range wrappers.

A borrowed decode view means: decode the CBOR string header and payload
boundaries, but return a view that points into the input buffer instead of
copying the payload into a new `std::string` or `std::vector`.

```cpp
std::vector<std::byte> data;
auto enc = make_encoder(data);
enc(std::string{"hello"});

auto dec = make_decoder(data);
std::string_view text;
dec(text);
```

`text` points into `data`. Keep `data` alive and do not invalidate it while
using `text`.

Borrowed text and byte-string views decode definite strings only. If the input
may contain indefinite/chunked text or byte strings, decode into an owning
`std::string` or byte container instead.

For non-contiguous input buffers, use the decoder-provided aliases:

```cpp
std::deque<std::byte> data;
auto enc = make_encoder(data);
enc(std::string{"hello"});

auto dec = make_decoder(data);
decltype(dec)::tstr_view_t text;
dec(text);
```

## Raw Encoded Views

Raw encoded views are for already-encoded CBOR items.

Use them when you want to copy, forward, or embed a complete CBOR item without
decoding it into a C++ value and encoding it again.

```cpp
std::vector<std::byte> data;
auto enc = make_encoder(data);
enc(std::vector<int>{1, 2, 3});

auto dec = make_decoder(data);
decltype(dec)::raw_encoded_array_view raw;
dec(raw);

std::vector<std::byte> forwarded;
auto out = make_encoder(forwarded);
out(raw); // re-emits the same encoded array bytes
```

The raw view borrows from the decoder input. For contiguous input, `raw.span()`
is available. For all supported input buffers, `raw.bytes()` can be iterated.

## Lazy Tag Scanning

Lazy tag scanning is the part that is actually lazy about decoding payloads.

`find_tags` scans nested CBOR structure and returns matches for the requested
tag numbers. It does not materialize unrelated payloads as C++ objects. You
decode a matched payload only when you ask for it.

```cpp
auto matches = find_tags<100, 200>(buffer);

for (const auto& match : matches) {
    if (match.tag() != 100) {
        continue;
    }

    MyPayload payload{};
    auto result = match.decode(payload);
}

if (matches.failed()) {
    auto status = matches.status();
}
```

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

`cbor_segments` is different. It stores output as a list of byte segments. Small
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
cbor_segments segments;
auto enc = make_encoder(segments);
enc(as_array{2}, 1, 2);

auto contiguous = segments.flatten();
```

Append helpers let you build segmented output incrementally:

```cpp
cbor_segments segments;
auto payload_span = std::span<const std::byte>{payload};

encode_bstr_segments_into(segments, payload_span);
encode_indefinite_bstr_segments_into(segments, payload_span, 4096);
encode_tagged_bstr_segments_into(segments, std::uint64_t{24}, payload_span);
```

Flattening always copies. Borrowed segment payloads must outlive the segment
container.

## Convert Raw Views To Segments

Use `to_segments` or `copy_segments` when the segment output should own the raw
encoded bytes.

```cpp
auto dec = make_decoder(buffer);
decltype(dec)::raw_encoded_array_view raw;
dec(raw);

auto owned = to_segments(raw);
```

Use `borrow_segments` only when the raw view came from contiguous lvalue input
that will outlive the segments.

```cpp
std::vector<std::byte> buffer{std::byte{0x82}, std::byte{0x01}, std::byte{0x02}};
auto dec = make_decoder(buffer);
decltype(dec)::raw_encoded_array_view raw;
dec(raw);

auto borrowed = borrow_segments(raw);
```

## Wrap Encoded Items In Bstr

Validated encoded item segments can be wrapped as a CBOR byte string without
flattening first.

```cpp
std::vector<int> values{1, 2};
auto encoded = encode_item_segments(values);

std::vector<std::byte> output;
auto enc = make_encoder(output);

if (encoded) {
    enc(as_bstr(*encoded));
}
```

## Which API Should I Use?

Use normal containers when you already have the value in the shape you want:

```cpp
enc(std::vector<int>{1, 2, 3});
enc(std::string{"Ada"});
```

Use range wrappers when you have a view, generator, transform, filter, span, or
ambiguous range and need to say which CBOR type to emit:

```cpp
enc(as_array_range(filtered_values));
enc(as_map_range(generated_pairs));
enc(as_tstr_range(characters));
enc(as_bstr_range(bytes));
```

Use borrowed decode views when the input buffer already exists and you want a
view into it instead of a copied string or byte vector.

Use raw encoded views when the payload is already CBOR and you want to forward
it without decoding it.

Use lazy tag scanning when you have a larger CBOR document and only care about
specific tagged payloads.

Use `cbor_segments` when the output should be written as pieces and large
payloads should not be copied into one contiguous output vector.

## Rules To Remember

- Range wrappers encode immediately when passed to the encoder.
- Range wrappers select CBOR shape explicitly: array, map, text string, or byte
  string.
- Sized ranges emit definite arrays, maps, text strings, or byte strings.
- Non-sized array and map ranges emit indefinite arrays and maps.
- Non-sized text and byte ranges emit indefinite strings made from definite
  chunks.
- Borrowed decode views, raw encoded views, lazy tag matches, and borrowed
  segments reference the original buffer.
- Borrowed decode views for text and byte strings require definite string
  payloads; use owning strings or byte containers for indefinite strings.
- Segment-backed encoders may borrow contiguous lvalue range payloads; keep the
  source alive and unchanged until the segments are flattened or written.
- `payload_span()` and `borrow_segments()` require contiguous input.
- Non-contiguous text and byte views use decoder-provided aliases.
- `flatten_segments(...)` produces a contiguous copy.
