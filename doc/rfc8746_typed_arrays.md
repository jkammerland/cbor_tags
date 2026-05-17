# RFC 8746 Typed Arrays

`cbor_tags/extensions/rfc8746_typed_arrays.h` provides an opt-in codec for a
selected set of RFC 8746 typed-array tags. Include the extension and install
`typed_array_codec` on the encoder or decoder:

```cpp
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/rfc8746_typed_arrays.h"

using namespace cbor::tags;
using namespace cbor::tags::ext::rfc8746;

std::vector<std::int32_t> values{1, -2, 3};
std::vector<std::byte> bytes;

auto enc = make_encoder<typed_array_codec>(bytes);
enc(as_typed_array(values));

typed_array<std::int32_t> decoded;
auto dec = make_decoder<typed_array_codec>(bytes);
dec(decoded);
```

Supported element types:

| C++ type | Byte order | CBOR tag | Type |
|----------|------------|---------:|------|
| `std::int32_t` | little | 78 | `typed_array<std::int32_t>` |
| `std::int64_t` | little | 79 | `typed_array<std::int64_t>` |
| `float16_t` | little | 84 | `typed_array<float16_t>` |
| `float` | little | 85 | `typed_array<float>` |
| `double` | little | 86 | `typed_array<double>` |
| `float` | big | 81 | `typed_array_be<float>` |
| `double` | big | 82 | `typed_array_be<double>` |

The wire shape is:

```cddl
ta-sint32le = #6.78(bstr)
ta-sint64le = #6.79(bstr)
ta-float16le = #6.84(bstr)
ta-float32le = #6.85(bstr)
ta-float64le = #6.86(bstr)
ta-float32be = #6.81(bstr)
ta-float64be = #6.82(bstr)
```

The byte string contains tightly packed element payload bytes in the selected
byte order.

## Owning And Borrowed Forms

Use `as_typed_array(values)` to encode an existing span or vector without
changing the application type:

```cpp
std::vector<double> values{1.0, -2.5};
enc(as_typed_array(values));
enc(as_typed_array_be(values));
```

Decode into `typed_array<T>` when you want an owning `std::vector<T>`:

```cpp
typed_array<double> decoded;
dec(decoded);

auto& values = decoded.values();
```

Use `typed_array_be<T>` for big-endian float and double payloads:

```cpp
typed_array_be<double> decoded_be;
dec(decoded_be);
```

Decode into `typed_array_view<T>` when you want to borrow the encoded payload
and materialize values lazily:

```cpp
typed_array_view<double> view;
dec(view);

for (double value : view.values()) {
    // use value
}
```

For big-endian payload views, use `typed_array_view_be<T>`:

```cpp
typed_array_view_be<float> view;
dec(view);
```

For non-contiguous input buffers, use `typed_array_view_for<T, Decoder>` so the
view stores the decoder's non-contiguous byte-string view type:

```cpp
auto dec = make_decoder<typed_array_codec>(input);
typed_array_view_for<std::int32_t, decltype(dec)> view;
dec(view);
```

Use `typed_array_view_be_for<T, Decoder>` for non-contiguous big-endian views.

## Segmented Output

For segmented output buffers, the extension can emit tag and payload segments
directly:

```cpp
auto segments = encode_typed_array_segments(std::span<const std::int32_t>{values});
```

When the requested byte order matches native byte order, the payload segment can
borrow from the input span. On common little-endian hosts this means
`encode_typed_array_segments(...)` is zero-copy for the little-endian tags, while
big-endian output should use `encode_typed_array_segments_copy_be(...)`.

## Variants

`typed_array<T>` and `typed_array_view<T>` are fixed-tag decode targets, so they
can participate in unambiguous `std::variant` decode:

```cpp
using value_type = std::variant<
    typed_array<std::int32_t>, // #6.78
    typed_array<double>,       // #6.86
    typed_array_be<double>,    // #6.82
    static_tag<42>
>;
```

`static_tag<N>` is tag-header-only. It consumes the tag header and leaves the tag
content for the next decode, so use it only when intentionally parsing the tag
content separately.

Variant tag collisions are rejected at compile time when decode is instantiated:

```cpp
using duplicate_static_tag = std::variant<typed_array<std::int32_t>, static_tag<78>>;

duplicate_static_tag value;
auto dec = make_decoder<typed_array_codec>(bytes);
dec(value); // Error: both alternatives match #6.78.

using catch_all_collision = std::variant<typed_array<std::int32_t>, as_tag_any>;
using duplicate_typed_array_tag = std::variant<typed_array<std::int32_t>, typed_array_view<std::int32_t>>;
```

`typed_array_ref<T>` is an encode wrapper and is not a variant decode target.

## Validation

Decode rejects:

- wrong typed-array tags,
- non-byte-string payloads,
- indefinite-length byte strings,
- payload sizes that are not a multiple of `sizeof(T)`,
- truncated byte-string headers or payloads.

CDDL generation does not currently render RFC 8746 typed-array extension types
as specialized schemas. Define the typed-array rules manually when documenting a
schema that uses these wrappers.
