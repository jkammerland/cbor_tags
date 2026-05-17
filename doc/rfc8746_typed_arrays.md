# RFC 8746 Typed Arrays

`cbor_tags/extensions/rfc8746_typed_arrays.h` provides an opt-in codec for RFC
8746 typed-array tags. Include the extension and install
`typed_array_codec` on the encoder or decoder:

```cpp
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/rfc8746_typed_arrays.h"

#include <cstddef>
#include <cstdint>
#include <vector>

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
| `std::uint8_t` | n/a | 64 | `typed_array<std::uint8_t>` |
| `std::uint16_t` | big | 65 | `typed_array_be<std::uint16_t>` |
| `std::uint32_t` | big | 66 | `typed_array_be<std::uint32_t>` |
| `std::uint64_t` | big | 67 | `typed_array_be<std::uint64_t>` |
| `uint8_clamped` | n/a | 68 | `typed_array<uint8_clamped>` |
| `std::uint16_t` | little | 69 | `typed_array<std::uint16_t>` |
| `std::uint32_t` | little | 70 | `typed_array<std::uint32_t>` |
| `std::uint64_t` | little | 71 | `typed_array<std::uint64_t>` |
| `std::int8_t` | n/a | 72 | `typed_array<std::int8_t>` |
| `std::int16_t` | big | 73 | `typed_array_be<std::int16_t>` |
| `std::int32_t` | big | 74 | `typed_array_be<std::int32_t>` |
| `std::int64_t` | big | 75 | `typed_array_be<std::int64_t>` |
| `std::int16_t` | little | 77 | `typed_array<std::int16_t>` |
| `std::int32_t` | little | 78 | `typed_array<std::int32_t>` |
| `std::int64_t` | little | 79 | `typed_array<std::int64_t>` |
| `float16_t` | big | 80 | `typed_array_be<float16_t>` |
| `float` | big | 81 | `typed_array_be<float>` |
| `double` | big | 82 | `typed_array_be<double>` |
| `float128_t` | big | 83 | `typed_array_be<float128_t>` |
| `float16_t` | little | 84 | `typed_array<float16_t>` |
| `float` | little | 85 | `typed_array<float>` |
| `double` | little | 86 | `typed_array<double>` |
| `float128_t` | little | 87 | `typed_array<float128_t>` |

The wire shape is:

```cddl
ta-uint8 = #6.64(bstr)
ta-uint16be = #6.65(bstr)
ta-uint32be = #6.66(bstr)
ta-uint64be = #6.67(bstr)
ta-uint8-clamped = #6.68(bstr)
ta-uint16le = #6.69(bstr)
ta-uint32le = #6.70(bstr)
ta-uint64le = #6.71(bstr)
ta-sint8 = #6.72(bstr)
ta-sint16be = #6.73(bstr)
ta-sint32be = #6.74(bstr)
ta-sint64be = #6.75(bstr)
; reserved: #6.76(bstr)
ta-sint16le = #6.77(bstr)
ta-sint32le = #6.78(bstr)
ta-sint64le = #6.79(bstr)
ta-float16be = #6.80(bstr)
ta-float32be = #6.81(bstr)
ta-float64be = #6.82(bstr)
ta-float128be = #6.83(bstr)
ta-float16le = #6.84(bstr)
ta-float32le = #6.85(bstr)
ta-float64le = #6.86(bstr)
ta-float128le = #6.87(bstr)
homogeneous<array> = #6.41(array)
multi-dim<dim, array> = #6.40([dim, array])
multi-dim-column-major<dim, array> = #6.1040([dim, array])
```

The byte string contains tightly packed element payload bytes in the selected
byte order. `float128_t` is an opaque 16-byte payload holder because C++20 has
no portable IEEE 754 binary128 value type. Its bytes are interpreted as a
native-endian binary128 object representation; `as_typed_array_be()` and
`as_typed_array()` convert that representation to the selected wire byte order.
If you already have preformatted wire-order bytes, emit a tagged byte string
directly instead of wrapping them in `float128_t`.

The structural tags are exposed as fixed-tag wrappers:

```cpp
std::vector<int> values{1, 2, 3};
enc(as_homogeneous_array(values)); // #6.41(array)

std::vector<std::uint64_t> dimensions{2, 2};
typed_array<std::uint16_t> cells{{1, 2, 3, 4}};
enc(as_multi_dimensional_array(dimensions, cells));              // #6.40([dim, array])
enc(as_multi_dimensional_column_major_array(dimensions, cells)); // #6.1040([dim, array])
```

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

## CDDL Rendering

`cddl_schema_to<T>` renders the RFC 8746 wrappers as semantic tags around their
wire payload shape:

```cpp
#include "cbor_tags/extensions/cbor_visualization.h"
#include "cbor_tags/extensions/rfc8746_typed_arrays.h"

#include <fmt/format.h>

fmt::memory_buffer schema;
cddl_schema_to<typed_array<std::int32_t>>(schema);
// root = #6.78(bstr)

fmt::memory_buffer schema_be;
cddl_schema_to<typed_array_be<double>>(schema_be);
// root = #6.82(bstr)
```

Structural wrappers render their actual encoded payload:

```cpp
fmt::memory_buffer homogeneous_schema;
cddl_schema_to<homogeneous_array<std::vector<int>>>(homogeneous_schema);
// root = #6.41([* int])

using matrix = multi_dimensional_array<
    std::vector<std::uint64_t>,
    typed_array<std::uint16_t>>;

fmt::memory_buffer matrix_schema;
cddl_schema_to<matrix>(matrix_schema);
// root = #6.40([[* uint], #6.69(bstr)])
```

The structural wrappers are shape wrappers, not full semantic validators.
`homogeneous_array<T>` requires `T` to encode as a CBOR array, and
`multi_dimensional_array<Dimensions, T>` requires unsigned-integer array
dimensions plus an array, typed-array, or homogeneous-array payload.
Multi-dimensional encode/decode rejects zero dimensions and checks
dimension/product consistency when the payload element count is available from
the C++ type.

The generated scalar typed-array CDDL uses the RFC data-model spelling
`#6.N(bstr)`. The decoder accepts definite-length byte strings for typed-array
payloads and rejects indefinite-length byte strings.

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

## Endian Performance

Prefer the little-endian typed-array tags on little-endian hosts when you control
both sides of the protocol. The little-endian path can copy or borrow the native
payload bytes directly. Big-endian tags are fully supported for interoperability,
but each multi-byte element must be byte-swapped on little-endian machines.

Local Release benchmark, 65,536 elements, Linux x86-64:

| Operation | LE | BE |
|-----------|----:|----:|
| `float` encode | 81.57 GB/s | 16.31 GB/s |
| `double` encode | 70.66 GB/s | 26.36 GB/s |
| `float` owning decode | 54.83 GB/s | 17.14 GB/s |
| `double` owning decode | 50.36 GB/s | 27.36 GB/s |
| `float` view materialize | 21.47 GB/s | 13.30 GB/s |
| `double` view materialize | 42.09 GB/s | 26.12 GB/s |

CBOR's scalar integer encoding is optimized for canonical interchange, not raw
numeric lanes: negative integers use major type 1 rather than two's-complement
payload bytes, and multi-byte numeric scalar conventions are network-order
oriented. RFC 8746 typed arrays avoid that per-element scalar overhead. On
little-endian hosts, the little-endian typed-array tags are the high-throughput
choice.

SIMD acceleration for the big-endian conversion path is intentionally left as
future work. The current scalar byteswap path is portable and correct, and users
who control both sides of a protocol should prefer the little-endian tags on
little-endian hosts. If a workload genuinely needs high-throughput big-endian
typed arrays, that SIMD path should be benchmark-driven and architecture-specific.
See [issue #39](https://github.com/jkammerland/cbor_tags/issues/39).

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
`typed_array_view<T>` is a decode/CDDL view; it can appear as a decoded
multi-dimensional payload type, but encode factories require an encodable
payload such as a CBOR array, `typed_array<T>`, `typed_array_ref<T>`, or
homogeneous array wrapper.

## Support Checklist

- all RFC scalar definite-length byte-string typed-array tags `64..87`, except reserved tag `76`,
- `uint8_clamped` and opaque `float128_t`,
- little-endian and big-endian integer and floating-point arrays,
- owning typed arrays and borrowed typed-array views,
- non-contiguous input view aliases,
- borrowed encode wrappers,
- segmented output helpers,
- variant decode by fixed tag with compile-time collision checks,
- structural tags `#6.40`, `#6.41`, and `#6.1040`,
- CDDL rendering for scalar typed arrays and structural wrappers.

## Validation

Decode rejects:

- wrong typed-array tags,
- non-byte-string payloads,
- indefinite-length byte strings,
- payload sizes that are not a multiple of `sizeof(T)`,
- truncated byte-string headers or payloads.
