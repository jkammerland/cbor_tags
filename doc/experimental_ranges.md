# Experimental Range And Segment APIs

> [!WARNING]
> EXPERIMENTAL. These APIs are still WIP. Names and exact borrowing behavior may
> change before they are treated as stable API.

This page shows the current user-facing range, raw-view, lazy tag, and segment
APIs. The examples assume:

```cpp
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_ranges.h>

#include <cbor_tags/cbor_lazy_tags.h>
#include <cbor_tags/cbor_segments.h>

using namespace cbor::tags;
```

## Encode Views As Arrays Or Maps

Use explicit wrappers when a C++ range should become a CBOR array or map.

```cpp
std::vector<std::byte> out;
auto enc = make_encoder(out);

std::vector<int> values{1, 2, 3};
enc(as_array_range(values)); // 83 01 02 03

auto pairs = std::views::iota(1, 4)
           | std::views::transform([](int value) {
                 return std::pair{value, value * 10};
             });

enc(as_map_range(pairs)); // a3 01 0a 02 14 03 18 1e
```

Sized ranges encode as definite arrays/maps. Non-sized ranges encode as
indefinite arrays/maps. Pair ranges are maps only when wrapped with
`as_map_range`.

## Encode Text Or Byte Ranges

Use `as_tstr_range` when a char range should become a CBOR text string.
The wrapper accepts `char` and other signed one-byte character ranges. It does
not validate UTF-8.

```cpp
std::string name{"Ada"};

auto upper = name | std::views::transform([](char value) {
    return static_cast<char>(value >= 'a' && value <= 'z' ? value - 'a' + 'A' : value);
});

enc(as_tstr_range(upper)); // 63 41 44 41
```

Use `as_bstr_range` when a byte-like range should become a CBOR byte string.

```cpp
std::array<unsigned char, 5> source{0, 1, 2, 3, 4};

auto bytes = source | std::views::transform([](unsigned char value) {
    return static_cast<std::byte>(value);
});

enc(as_bstr_range(bytes)); // 45 00 01 02 03 04
```

For non-sized text or byte ranges, pass a chunk size to emit an indefinite
string in definite chunks.

```cpp
auto every_char = name | std::views::filter([](char) {
    return true;
});

enc(as_tstr_range(every_char, 2)); // 7f 62 41 64 61 61 ff

auto odd_bytes = source | std::views::filter([](unsigned char value) {
    return (value % 2U) == 1U;
}) | std::views::transform([](unsigned char value) {
    return static_cast<std::byte>(value);
});

enc(as_bstr_range(odd_bytes, 2)); // 5f 42 01 03 ff
```

`chunk_size` defaults to `4096` and must be greater than zero.

## Decode Into Borrowed Views

Contiguous buffers can decode text and byte strings into normal view types.

```cpp
std::vector<std::byte> data;
auto enc = make_encoder(data);
enc(std::string{"hello"});

auto dec = make_decoder(data);
std::string_view text;
dec(text);
```

For non-contiguous input, use the decoder-provided view aliases.

```cpp
std::deque<std::byte> data;
auto enc = make_encoder(data);
enc(std::string{"hello"});

auto dec = make_decoder(data);
decltype(dec)::tstr_view_t text;
dec(text);
```

The returned view borrows from `data`. Keep the input buffer alive and do not
invalidate its iterators while the view is used.

## Reuse Raw Encoded Items

Decode a complete CBOR item as raw encoded bytes when you want to forward or
embed it without decoding the payload type.

```cpp
std::vector<std::byte> data;
auto enc = make_encoder(data);
enc(std::vector<int>{1, 2, 3});

auto dec = make_decoder(data);
decltype(dec)::raw_encoded_array_view raw;
dec(raw);

std::vector<std::byte> forwarded;
auto out = make_encoder(forwarded);
out(raw);
```

Raw views borrow from the decoder input. For contiguous input, `raw.span()` is
available. For all inputs, `raw.bytes()` can be iterated.

## Scan Tags Lazily

`find_tags` scans nested CBOR and returns matching tags without materializing
unrelated payloads.

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

Runtime filters are also supported.

```cpp
auto even_tags = find_tags(buffer, [](std::uint64_t tag) {
    return (tag % 2U) == 0U;
});
```

Use `payload_range()` for encoded payload bytes. Use `payload_span()` only when
the original buffer is contiguous.

## Emit Segmented Output

`cbor_segments` keeps output as owned headers plus borrowed payload spans. This
is useful when a large byte payload already exists and should not be copied into
one contiguous output buffer.

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

You can also use segments as the encoder output backend.

```cpp
cbor_segments segments;
auto enc = make_encoder(segments);
enc(as_array{2}, 1, 2);

auto contiguous = segments.flatten();
```

Append helpers let you build a segmented output incrementally.

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

## Rules To Remember

- Range wrappers select CBOR shape explicitly: array, map, text string, or byte
  string.
- Views, lazy tag matches, and borrowed segments reference the original buffer.
- Segment-backed encoders may borrow contiguous lvalue range payloads; keep the
  source alive and unchanged until the segments are flattened or written.
- `payload_span()` and `borrow_segments()` require contiguous input.
- Non-contiguous text and byte views use decoder-provided aliases.
- `flatten_segments(...)` produces a contiguous copy.
