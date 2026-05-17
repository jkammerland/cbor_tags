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
- `enum_mode`: Keep enums as underlying `uint`/`int` shapes by default, or emit named CDDL enumeration choices with `CDDLEnumMode::named_values` when `CBOR_TAGS_USE_MAGIC_ENUM_NAMES=ON`

Generated schemas mirror the default encoder shape plus explicitly documented
transform/extension shapes. Multi-field aggregates are arrays, single-field
aggregates are the single payload value, maps are rendered as
`{* key => value}`, and sequence containers are rendered as `[* value]`.
Static tags render as `#6.n(payload)`. Dynamic tag values are not available
from the type alone, so dynamic tags render as `#6(payload)`. Recursive
aggregate types are emitted as named CDDL rules.

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

### C++26 Named Maps

When `CBOR_TAGS_USE_STD_REFLECTION=ON`, reflected member names can be used as
CBOR map keys through the explicit named-map transform:

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
Shared pointer identity and `shared_graph_codec` reference-table semantics are
runtime codec rules and are not expressed in CDDL; the graph codec uses CBOR
value-sharing tags 28 and 29 at runtime. As a conservative generator limitation,
`std::variant` alternatives that contain nullable smart pointers are rejected by
the CDDL generator, including tagged alternatives that may be runtime-decodable
through an opt-in codec.

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
- [fmtlib](https://github.com/fmtlib/fmt)
- [nameof](https://github.com/Neargye/nameof)
- [magic_enum](https://github.com/Neargye/magic_enum), optional for named enum CDDL in C++20 builds

## Documentation

For advanced usage and implementation details, see the header files and test cases. Reference the [CBOR RFC](https://tools.ietf.org/html/rfc8949) and [CDDL spec](https://tools.ietf.org/html/rfc8610) for schema conventions.
For feature-by-feature standards coverage, see [CDDL Standard Coverage](cddl_standard_coverage.md).
