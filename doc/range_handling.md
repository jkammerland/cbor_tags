# Range And View Handling

`cbor_tags` accepts byte buffers whose `value_type` is `std::byte` or a
non-`bool` one-byte integral type, and whose `size_type` is convertible to
`std::size_t`. Common accepted byte storage types include `std::byte`, `char`,
`signed char`, `unsigned char`, `std::uint8_t`, and `std::int8_t` when those
integer aliases are one byte on the target.

Supported CBOR input/output buffers are:

- Contiguous ranges, such as `std::vector<std::byte>`, `std::array<std::byte, N>`, and `std::span<std::byte>`.
- Bidirectional non-contiguous ranges, such as `std::deque<std::byte>` and `std::list<std::byte>`.

Forward-only non-contiguous ranges are intentionally not valid CBOR buffers.
Decoder retry paths need to step back one byte after a failed alternative match.
For non-contiguous buffers this requires a bidirectional iterator; accepting a
forward-only range would compile an unsafe rewind path.

## Owning Decode Targets

Owning decode targets copy decoded bytes or elements into the destination value.
The destination can outlive the input buffer.

Common mappings:

- CBOR byte string (`bstr`) to `std::vector<std::byte>`, `std::array<std::byte, N>`, or another binary string type.
- CBOR text string (`tstr`) to `std::string`.
- CBOR array to sequence containers such as `std::vector<T>`, `std::deque<T>`, `std::list<T>`, or `std::array<T, N>`.
- CBOR map to map-like containers such as `std::map<K, V>` and `std::unordered_map<K, V>`.

Example:

```cpp
std::vector<std::byte> data;
auto enc = cbor::tags::make_encoder(data);
enc(std::vector<int>{1, 2, 3});

std::vector<int> values;
auto dec = cbor::tags::make_decoder(data);
dec(values);
```

## View Decode Targets

View decode targets avoid copying. They reference the original input buffer.
The input buffer must remain alive and must not be reallocated or otherwise
mutated in a way that invalidates iterators while the view is used.

Contiguous input buffers can decode views as:

- `std::string_view` for text strings.
- `std::span<const std::byte>` or `std::basic_string_view<std::byte>` for byte strings.

Non-contiguous input buffers cannot produce contiguous views. Decode into the
decoder-provided range view aliases instead:

- `decltype(decoder)::tstr_view_t` for text strings.
- `decltype(decoder)::bstr_view_t` for byte strings.

Example:

```cpp
std::deque<std::byte> data;
auto enc = cbor::tags::make_encoder(data);
enc(std::string{"hello"});

auto dec = cbor::tags::make_decoder(data);
decltype(dec)::tstr_view_t text;
dec(text);

std::string copied{text};
```

Trying to decode `std::string_view` or `std::span<const std::byte>` from a
non-contiguous buffer fails with
`status_code::contiguous_view_on_non_contiguous_data`.

## Variant Range Surface

`variant_t<T>` chooses a compact view representation for low-level dynamic CBOR
values:

- Contiguous buffers use `std::span<const std::byte>`, `std::string_view`, and binary wrapper views for arrays, maps, and tags.
- Non-contiguous buffers use `bstr_view<R>` and `tstr_view<R>` for string-like data, plus scalar alternatives.

Non-contiguous array, map, and tag range-view placeholders are not part of the
public variant surface. Decode those structures into owning arrays, maps, tagged
types, or custom visitor targets instead.
