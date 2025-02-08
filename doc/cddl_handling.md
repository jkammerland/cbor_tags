# CBOR CDDL Schema Generator & Annotator

A C++ header-only library for generating CDDL schemas from C++ types and annotating CBOR data with diagnostic information.

## Features

- **Automatic CDDL Generation**: Convert C++ structs to CDDL schemas using compile-time reflection
- **CBOR Annotation**: Convert binary CBOR data to human-readable hex format with structure hints
- **Rich Type Support**: Handles variants, optionals, maps, vectors, and custom CBOR tags
- **Custom Formatting**: Control schema layout with row-based or inline formatting options

## Quick Start

```cpp
#include "cbor_tags/cbor_cddl.h"

struct MyStruct {
    int32_t id;
    std::string name;
    std::variant<bool, float> status;
};

// Generate CDDL schema
fmt::memory_buffer buffer;
cbor::tags::cddl_schema_to<MyStruct>(buffer);
// Output: 
// MyStruct = (
//   int,
//   tstr,
//   bool / float
// )
```

## Basic Usage

### Schema Generation

```cpp
struct Person {
    uint32_t age;
    std::map<std::string, int> attributes;
    std::optional<std::vector<byte>> data;
};

fmt::memory_buffer schema;
cbor::tags::cddl_schema_to<Person>(schema, {.row_options = {.format_by_rows = false}});
```

### CBOR Annotation

```cpp
std::vector<byte> cbor_data = /* ... */;
fmt::memory_buffer annotation;
buffer_annotate(cbor_data, annotation, {
    .current_indent = 2,
    .max_depth = 16
});
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
- `row_options.format_by_rows`: Format complex types across multiple lines
- `always_inline`: Prevent type definitions from being separated

### `buffer_annotate(cbor_buffer, output, options)`
Creates annotated hex view of CBOR data

**Options**:
- `indent_level`: Base indentation level
- `max_depth`: Wrap lines after N bytes
- `diagnostic_data`: (Future) Generate full diagnostic notation

## Dependencies

- C++20 compiler
- [fmtlib](https://github.com/fmtlib/fmt)
- [nameof](https://github.com/Neargye/nameof)

## Documentation

For advanced usage and implementation details, see the header files and test cases. Reference the [CBOR RFC](https://tools.ietf.org/html/rfc8949) and [CDDL spec](https://tools.ietf.org/html/rfc8610) for schema conventions.

---

> **Note**  
> This library is in active development. Report issues and contribute via GitHub.