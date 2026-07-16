# CBOR CDDL Schema Generator And Visualization

A C++ header-only library for generating CDDL schemas from C++ types and annotating CBOR data with diagnostic information.

## Features

- **Automatic CDDL Generation**: Convert C++ structs to CDDL schemas using compile-time reflection
- **CBOR Annotation**: Convert binary CBOR data to human-readable hex format with structure hints
- **Rich Type Support**: Handles variants, optionals, maps, vectors, and custom CBOR tags
- **Custom Formatting**: Control schema layout with row-based or inline formatting options

## Quick Start

```cpp
#include "cbor_tags/extensions/cbor_visualization.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

struct MyStruct {
    int32_t id;
    std::string name;
    std::variant<bool, float> status;
};

// Generate CDDL schema
fmt::memory_buffer buffer;
cbor::tags::cddl_schema_to<MyStruct>(buffer);
// Output:
// MyStruct = [
//   int,
//   tstr,
//   bool / float32
// ]
```

## Basic Usage

### Schema Generation

```cpp
struct Person {
    uint32_t age;
    std::map<std::string, int> attributes;
    std::optional<std::vector<std::byte>> data;
};

fmt::memory_buffer schema;
cbor::tags::cddl_schema_to<Person>(schema, {.row_options = {.format_by_rows = false}});
// Person = [uint, {* tstr => int}, bstr / null]
```

When named reflection is available, aggregate array fields can be labeled
without changing the encoded CBOR shape:

```cpp
fmt::memory_buffer schema;
cbor::tags::cddl_schema_to<Person>(
    schema,
    {.row_options = {.format_by_rows = false}, .label_array_fields = true});
// Person = [age: uint, attributes: {* tstr => int}, data: bstr / null]
```

### CBOR Annotation

```cpp
std::vector<std::byte> cbor_data = /* ... */;
fmt::memory_buffer annotation;
cbor::tags::buffer_annotate(cbor_data, annotation, {
    .current_indent = 2,
    .max_depth = 16
});
```

`buffer_annotate` emits an annotated hex view. `buffer_diagnostic` emits a
diagnostic-notation-like value stream:

```cpp
fmt::memory_buffer diagnostic;
cbor::tags::buffer_diagnostic(cbor_data, diagnostic);
```

### Custom Tags

```cpp
struct CustomTagged {
    static constexpr uint64_t cbor_tag = 42;
    std::string value;
};

// Generates:
// CustomTagged = #6.42(tstr)
```

## API Highlights

### `cddl_schema_to<Type>(buffer, options)`
Generates CDDL schema for the given type into output buffer

**Options**:
- `row_options.format_by_rows`: Format multi-field aggregate payload arrays across multiple lines
- `always_inline`: Inline nested aggregate definitions when possible; recursive references stay named
- `root_name`: Override the generated root rule name; non-aggregate roots default to `root`
- `enum_mode`: Keep enums as underlying `uint`/`int` shapes by default, or emit named CDDL enumeration choices with `CDDLEnumMode::named_values` when C++26 static reflection or `CBOR_TAGS_USE_MAGIC_ENUM_NAMES=ON` is available
- `label_array_fields`: Label aggregate array fields when named reflection is available; single-field aggregates use the field name as a rule alias and reject alias collisions, including CDDL prelude names

Generated schemas mirror the default encoder shape plus explicitly documented
transform/extension shapes. Multi-field aggregates are arrays, single-field
aggregates are the single payload value, maps are rendered as
`{* key => value}`, and sequence containers are rendered as `[* value]`.
Static tags render as `#6.n(payload)`. Dynamic tag values are not available
from the type alone, so dynamic tags render as `#6(payload)`. Recursive
aggregate types are emitted as named CDDL rules.

### Size-Bounded Containers

Use `cbor::tags::bounded_size<T, Min, Max>` when a text string, byte string,
array-like container, map-like container, or supported extension wrapper has
protocol size bounds. For materialized strings and containers, the generated
CDDL includes the bound and the wrapper enforces the same bound while encoding
and decoding:

```cpp
namespace ct = cbor::tags;

struct Person {
    ct::bounded_size<std::string, 1, 64> name;
    ct::bounded_size<std::vector<int>, 1, 3> scores;
    ct::bounded_size<std::map<std::string, std::uint64_t>, 0, 2> attrs;
};

fmt::memory_buffer schema;
ct::cddl_schema_to<Person>(
    schema,
    {.row_options = {.format_by_rows = false}});
// Person = [tstr .size (1..64), [1*3 int], {0*2 tstr => uint}]
```

For root values or temporary views, `as_bounded_size<Min, Max>(value)` creates a
wrapper without changing the owned type:

```cpp
namespace ct = cbor::tags;

std::vector<int> values{1, 2, 3};
auto enc = ct::make_encoder(output);
auto result = enc(ct::as_bounded_size<1, 3>(values));
```

Explicit range wrappers can be bounded for encode and CDDL generation when the
wrapped range is a sized range. The encoder reads `size()` before writing:

```cpp
namespace ct = cbor::tags;

std::vector<int> values{1, 2, 3, 4};

auto view = ct::as_array_range(values);
auto bounded_view = ct::as_bounded_size<0, 4>(view);
enc(bounded_view);
// CDDL: [0*4 int]
```

Bounded encoding of a non-sized range is unsupported. Checking it before output
would require a separate counting traversal. The unbounded range wrappers still
encode non-sized input in one pass:

```cpp
namespace ct = cbor::tags;

auto evens = values | std::views::filter(is_even);

enc(ct::as_array_range(evens));

// Unsupported: evens has no constant-time size().
enc(ct::as_bounded_size<0, 4>(ct::as_array_range(evens)));
```

`as_indefinite(container)` is also supported when the underlying container is a
sized range. The bound is checked before writing, and the encoded item remains
indefinite:

```cpp
namespace ct = cbor::tags;

std::vector<int> values{1, 2, 3};
enc(ct::as_bounded_size<0, 3>(ct::as_indefinite{values}));
// 9f 01 02 03 ff
```

`std::array` and fixed-extent `std::span` keep their exact CDDL sequence shape;
the configured bound must include that fixed extent. Unwrapped containers still
render as unbounded `[* value]` or `{* key => value}` and do not get automatic
schema-size validation.

A bound applies only to the immediately wrapped value. It does not implicitly
bound nested allocations:

```cpp
namespace ct = cbor::tags;

using row = std::vector<int>;
using rows = ct::max_size<std::vector<row>, 16>;
// Limits the number of rows. Each row remains unbounded.

using bounded_row = ct::max_size<row, 32>;
using bounded_rows = ct::max_size<std::vector<bounded_row>, 16>;
// Limits both the number of rows and the number of values in each row.
```

The bound describes the cardinality of one encoded or decoded CBOR item. It is
not a persistent invariant on the backing C++ container. Decoding into an
existing mutable destination validates only the incoming item, then follows the
core decoder's append/insert contract:

```cpp
std::vector<int> incoming{1, 2};
std::vector<std::byte> input;
ct::make_encoder(input)(incoming);

std::vector<int> destination{9, 8, 7};
auto result = ct::make_decoder(input)(ct::as_bounded_size<2, 2>(destination));
// result succeeds; destination is now {9, 8, 7, 1, 2}.
// Its final size may exceed Max because Max constrained the incoming CBOR array.
```

Encoding validates the complete wrapped source value. To encode only a bounded
slice, make that slice the wrapped value instead of expecting the bound to select
elements:

```cpp
std::vector<int> values{1, 2, 3, 4};

auto rejected = enc(ct::as_bounded_size<2, 2>(values)); // size 4: rejected
auto tail = std::span{values}.last<2>();
auto accepted = enc(ct::as_bounded_size<2, 2>(ct::as_array_range(tail)));
```

For definite containers, the decoder validates the declared size before
reserving target storage, then continues decoding that payload. It does not run
a validation pre-scan. Indefinite containers are validated while they are
decoded. If an indefinite value exceeds its maximum, or reaches its break before
its minimum, decoding returns `status_code::size_limit_exceeded` and leaves the
successfully decoded prefix in the target.

The core encoder and decoder recognize bounded CBOR text strings, byte strings,
arrays, and maps. An extension wrapper opts in with an ordinary codec overload;
no library trait registration is required. When the extension contains a core
container, its overload can delegate the bound without rereading the CBOR
header:

```cpp
struct samples {
    std::vector<int> values;
};

template <typename Self>
struct samples_codec : cbor::tags::cbor_codec_mixin_base<Self> {
    using cbor::tags::cbor_codec_mixin_base<Self>::decode;
    using cbor::tags::cbor_codec_mixin_base<Self>::encode;

    template <std::size_t Min, std::size_t Max>
    void encode(const cbor::tags::bounded_size<samples, Min, Max>& bounded) {
        static_cast<Self&>(*this).encode(
            cbor::tags::as_bounded_size<Min, Max>(bounded.value().values));
    }

    template <std::size_t Min, std::size_t Max>
    cbor::tags::status_code decode(
        cbor::tags::bounded_size<samples, Min, Max>& bounded,
        cbor::tags::major_type major,
        std::byte additional_info) {
        auto values = cbor::tags::as_bounded_size<Min, Max>(bounded.value().values);
        return static_cast<Self&>(*this).decode(values, major, additional_info);
    }
};
```

The RFC 8746 scalar typed-array extension supports `bounded_size` as element
counts. CDDL renders the corresponding byte-string byte count because the wire
payload is a `bstr`:

```cpp
namespace ct = cbor::tags;
namespace rfc8746 = cbor::tags::ext::rfc8746;

using samples = ct::bounded_size<rfc8746::typed_array<std::int32_t>, 1, 3>;

ct::cddl_schema_to<samples>(schema, {.row_options = {.format_by_rows = false}});
// root = #6.78(bstr .size (4..12))
```

Bounds for RFC 8746 `homogeneous_array` and `multi_dimensional_array` wrappers
are intentionally deferred.

Extension headers may add CDDL support by specializing the public CDDL traits
in `cbor::tags::cddl`, declared by
`cbor_tags/extensions/cddl_traits.h`. Simple tagged extension types expose fixed
tag metadata, while scoped wrappers can select a different rendering policy for
a whole schema root:

```cpp
namespace cbor::tags::cddl {
template <> struct cddl_tagged_bstr_array_traits<MyTypedBstrView> {
    static constexpr std::uint64_t tag = 1000;
};
}
```

For example,
`cbor::tags::ext::smart_ptr::shared_graph_cddl<T>` switches only that schema
root to the shared-graph `std::shared_ptr<T>` shape without changing the default
nullable pointer schema.

### Enum Names

By default, C++ enum types render as their CBOR integer shape because the
decoder accepts any value representable by the enum's underlying type:

```cpp
enum class Color : std::uint8_t { red = 1, green = 2, blue = 4 };

fmt::memory_buffer schema;
cbor::tags::cddl_schema_to<Color>(schema, {.row_options = {.format_by_rows = false}});
// root = uint
```

When the library is built with `CBOR_TAGS_USE_STD_REFLECTION=ON` or
`CBOR_TAGS_USE_MAGIC_ENUM_NAMES=ON`, CDDL output can opt into declared
enumerator values:

```cpp
fmt::memory_buffer named_schema;
cbor::tags::cddl_schema_to<Color>(
    named_schema,
    {.row_options = {.format_by_rows = false}, .enum_mode = cbor::tags::CDDLEnumMode::named_values});
// Color = &(red: 1, green: 2, blue: 4)
```

This schema is stricter than the default decoder policy: unnamed but
underlying-representable enum values still decode, while the generated named
CDDL choice only accepts the declared enumerators reported by native reflection
or magic_enum.

With `magic_enum`, declared enumerators are discovered from its configured scan
range. The default range is `[-128, 127]`; values outside that range are not
reported unless the application widens the range, for example with a
`magic_enum::customize::enum_range<T>` specialization defined before schema
generation.

### Named Maps

When named reflection is enabled through C++26 static reflection
(`CBOR_TAGS_USE_STD_REFLECTION=ON`) or C++20 Boost.PFR field names
(`CBOR_TAGS_USE_BOOST_PFR_NAMES=ON`), reflected member names can be used as CBOR
map keys through the explicit named-map transform:

```cpp
struct Person {
    int age;
    std::string name;
    std::string employer;
};

Person person{.age = 42, .name = "Ada", .employer = "AcmeCo"};
std::vector<std::byte> buffer;
auto enc = cbor::tags::make_encoder(buffer);
enc(cbor::tags::as_named_map{person});

fmt::memory_buffer schema;
cbor::tags::cddl_schema_to<cbor::tags::as_named_map<Person>>(
    schema, {.row_options = {.format_by_rows = false}, .root_name = "person"});
// person = {age: int, name: tstr, employer: tstr}
```

`std::optional<T>` named-map fields are omitted when empty and render as
`? key: T`. Use `as_named_group<T>` as a member to flatten a reusable CDDL
group into the surrounding map, and use
`as_named_extension<std::map<std::string, T>>` to capture unmatched text keys
and render `* tstr => T`. Unknown keys are rejected unless such an extension
field is present. Use only one flattened `as_named_extension` field per named
map shape; multiple extension fields are rejected at compile time because an
unknown key would not have a unique owner. Nested `as_named_map` members are
scoped maps and may have their own extension field. Fixed field names must also
be unique after flattening all `as_named_group` members; duplicate fixed names
are rejected at compile time.

`std::unique_ptr<T>` and `std::shared_ptr<T>` with default-initializable,
non-const, non-void, non-array pointee types render as `[0] / [1, T]`, matching
the opt-in `cbor::tags::ext::smart_ptr::nullable_ptr_codec` wire shape. Pointer
fields remain required in named maps unless the field type itself is
`std::optional`; a null pointer is an explicit `[0]`, not an omitted member.
For `shared_graph_codec` schemas, include `cbor_tags/extensions/smart_ptr.h` and
wrap the schema root in `cbor::tags::ext::smart_ptr::shared_graph_cddl<T>`.
Inside that scope, `std::shared_ptr<T>` renders as
`[0] / #6.28(T) / #6.29(uint)`, matching `as_shared_graph(...)` roots.
`shared_graph_cddl<T>` is a schema-root wrapper only; using it as a struct
member type is rejected.
Reference-table validity is still a runtime decoder-session rule and cannot be
fully expressed in CDDL. In the default nullable scope, `std::variant`
alternatives that contain nullable smart pointers are rejected by the CDDL
generator. In `shared_graph_cddl<T>`, variants may contain one direct nullable
smart pointer alternative when no array-shaped alternative or tag 28/29
collision is present. A direct `std::vector<std::shared_ptr<T>>` alternative is
also supported when it is the only array-shaped alternative, for example
`std::variant<std::vector<std::shared_ptr<T>>, std::string>`. Indirect forms
such as `std::optional<std::shared_ptr<T>>`, nested variants, maps, or vectors of
optionals remain unsupported inside `std::variant`.

### `buffer_annotate(cbor_buffer, output, options)`
Creates annotated hex view of CBOR data

**Options**:
- `current_indent`: Base indentation level
- `max_depth`: Plain/no_annotation mode wraps lines after N bytes; smart mode
  caps text/byte payload bytes per wrapped line
- `mode`: `AnnotationMode::smart` by default; set
  `AnnotationMode::no_annotation` for the old plain hex view
- `annotation_column`: Column where smart-mode `#` comments start
- `indent_width`: Left-column indentation step in smart mode
- `comment_indent_width`: Right-column nesting indentation step in smart mode
- `max_structure_depth`: Hard smart-mode nesting limit
- `max_input_size`: Hard smart-mode input size limit before copying bytes
- `max_output_size`: Hard smart-mode generated output limit to avoid unbounded
  memory growth
- `diagnostic_data`: Unsupported; setting it throws `std::runtime_error`

Smart mode keeps header comments aligned and wraps text/byte payload hex before
the annotation column. It throws on malformed CBOR, size limits, excessive
nesting, or layouts too narrow to format without truncation.

### `buffer_diagnostic(cbor_buffer, output, options)`
Creates diagnostic-notation-like output for the supported CBOR subset.

**Options**:
- `row_options.format_by_rows`: Format arrays and maps across multiple lines
- `row_options.override_array_by_columns`: Keep arrays inline when formatting by rows
- `check_tstr_utf8`: Validate text-string bytes as UTF-8 before rendering

Text strings are rendered from the bytes in the buffer by default. When
`check_tstr_utf8` is enabled, valid text strings still render as escaped text,
and invalid text strings render as `non-utf8(N)`, where `N` is byte length.

## Dependencies

- C++20 compiler
- [fmtlib](https://github.com/fmtlib/fmt), unless using the C++26 STL-only path
- [nameof](https://github.com/Neargye/nameof), unless using the C++26 STL-only path
- [magic_enum](https://github.com/Neargye/magic_enum), optional for named enum CDDL in C++20 builds

## Documentation

For advanced usage and implementation details, see the header files and test cases. Reference the [CBOR RFC](https://tools.ietf.org/html/rfc8949) and [CDDL spec](https://tools.ietf.org/html/rfc8610) for schema conventions.
For feature-by-feature standards coverage, see [CDDL Standard Coverage](cddl_standard_coverage.md).
