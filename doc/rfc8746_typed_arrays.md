# RFC 8746 Typed Arrays

`cbor_tags/extensions/rfc8746_typed_arrays.h` provides an opt-in codec for the
little-endian typed-array tags from RFC 8746. Include the extension and install
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

| C++ type | CBOR tag |
|----------|---------:|
| `std::int32_t` | 78 |
| `std::int64_t` | 79 |
| `float16_t` | 84 |
| `float` | 85 |
| `double` | 86 |

The wire shape is:

```cddl
typed-array<T> = #6.<tag>(bstr)
```

The byte string contains tightly packed little-endian element payload bytes.

## Owning And Borrowed Forms

Use `as_typed_array(values)` to encode an existing span or vector without
changing the application type:

```cpp
std::vector<double> values{1.0, -2.5};
enc(as_typed_array(values));
```

Decode into `typed_array<T>` when you want an owning `std::vector<T>`:

```cpp
typed_array<double> decoded;
dec(decoded);

auto& values = decoded.values();
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

For non-contiguous input buffers, use `typed_array_view_for<T, Decoder>` so the
view stores the decoder's non-contiguous byte-string view type:

```cpp
auto dec = make_decoder<typed_array_codec>(input);
typed_array_view_for<std::int32_t, decltype(dec)> view;
dec(view);
```

## Segmented Output

For segmented output buffers, the extension can emit tag and payload segments
directly:

```cpp
auto segments = encode_typed_array_segments(std::span<const std::int32_t>{values});
```

On little-endian hosts, the payload segment can borrow from the input span. Use
`encode_typed_array_segments_copy(...)` when you explicitly want an owned
little-endian payload.

## Variants

`typed_array<T>` and `typed_array_view<T>` are fixed-tag decode targets, so they
can participate in unambiguous `std::variant` decode:

```cpp
using value_type = std::variant<
    typed_array<std::int32_t>, // #6.78
    typed_array<double>,       // #6.86
    static_tag<42>
>;
```

Variant tag collisions are rejected at compile time:

```cpp
// Error: both alternatives match #6.78.
std::variant<typed_array<std::int32_t>, static_tag<78>> duplicate_static_tag;

// Error: catch-all tags can also match #6.78.
std::variant<typed_array<std::int32_t>, as_tag_any> catch_all_collision;

// Error: both alternatives match #6.78.
std::variant<typed_array<std::int32_t>, typed_array_view<std::int32_t>> duplicate_typed_array_tag;
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
as specialized schemas.
