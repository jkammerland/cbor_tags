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

Generated aggregate schemas mirror the default encoder shape. Multi-field
aggregates are arrays, single-field aggregates are the single payload value,
maps are rendered as `{* key => value}`, and sequence containers are rendered
as `[* value]`. Static tags render as `#6.n(payload)`. Dynamic tag values are
not available from the type alone, so dynamic tags render as `#6(payload)`.
Recursive aggregate types are emitted as named CDDL rules.

### `buffer_annotate(cbor_buffer, output, options)`
Creates annotated hex view of CBOR data

**Options**:
- `current_indent`: Base indentation level
- `max_depth`: Wrap lines after N bytes
- `mode`: `AnnotationMode::legacy` by default; set `AnnotationMode::smart`
  for semantic annotations in a right-hand column
- `annotation_column`: Column where smart-mode `#` comments start
- `indent_width`: Left-column indentation step in smart mode
- `comment_indent_width`: Right-column nesting indentation step in smart mode
- `max_structure_depth`: Hard smart-mode nesting limit
- `diagnostic_data`: Unsupported; setting it throws `std::runtime_error`

Smart mode keeps header comments aligned and wraps text/byte payload hex before
the annotation column. It throws on malformed CBOR or layouts too narrow to
format without truncation.

### `buffer_diagnostic(cbor_buffer, output, options)`
Creates diagnostic-notation-like output for the supported CBOR subset.

**Options**:
- `row_options.format_by_rows`: Format arrays and maps across multiple lines
- `row_options.override_array_by_columns`: Keep arrays inline when formatting by rows
- `check_tstr_utf8`: Unsupported; setting it throws `std::runtime_error`

Text strings are rendered from the bytes in the buffer. UTF-8 validation is not
implemented in this visualization layer.

## Dependencies

- C++20 compiler
- [fmtlib](https://github.com/fmtlib/fmt)
- [nameof](https://github.com/Neargye/nameof)

## Documentation

For advanced usage and implementation details, see the header files and test cases. Reference the [CBOR RFC](https://tools.ietf.org/html/rfc8949) and [CDDL spec](https://tools.ietf.org/html/rfc8610) for schema conventions.
For feature-by-feature standards coverage, see [CDDL Standard Coverage](cddl_standard_coverage.md).

---

> **Note**  
> This library is in active development. Report issues and contribute via GitHub.
