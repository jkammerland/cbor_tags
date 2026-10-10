# Learn cbor_tags one step at a time

Read the numbered programs in order. Each is a standalone C++20 program, with
its CDDL immediately above `main()` and short comments separating the steps.
Examples 01 and 03 group related basics; later files introduce one feature each.

## Learning order

| File | Purpose | New idea |
| --- | --- | --- |
| [01_basic_values.cpp](01_basic_values.cpp) | Encode and decode basic values | Integer, text, boolean, floating-point and byte-string values form a sequence, not an array. |
| [02_receiver_layout.cpp](02_receiver_layout.cpp) | Choose receiver-side types | The sender uses std::string; the receiver uses std::string_view for the same CBOR text. |
| [03_containers.cpp](03_containers.cpp) | Use arrays and maps | A vector is one array item; a map contains key/value pairs. |
| [04_struct.cpp](04_struct.cpp) | Reflect a struct | Public aggregate members become an array in declaration order. |
| [05_tagged_struct.cpp](05_tagged_struct.cpp) | Identify a struct | A static CBOR tag identifies the payload without becoming a field. |
| [06_optional.cpp](06_optional.cpp) | Represent absence | A present optional encodes its value; an empty optional encodes null. |
| [07_variant.cpp](07_variant.cpp) | Select a value type | Distinct CBOR major types select different variant alternatives. |
| [08_nested_arrays.cpp](08_nested_arrays.cpp) | Nest familiar containers | Arrays can contain other arrays without a new codec. |
| [09_decode_error.cpp](09_decode_error.cpp) | Inspect a failure | Decoding text into an integer returns a specific error. |
| [10_strict_integer_decoding.cpp](10_strict_integer_decoding.cpp) | Check integer ranges | Strict decoding rejects an integer that does not fit its destination. |
| [11_indefinite_array.cpp](11_indefinite_array.cpp) | Omit the array length | An indefinite array ends with a break token; ordinary vector decoding consumes it. |
| [12_array_header.cpp](12_array_header.cpp) | Read a header separately | The header declares a count and leaves the elements unread. |
| [13_item_validation.cpp](13_item_validation.cpp) | Validate one item | Validation checks the next complete item and leaves subsequent items unread. |
| [14_item_visitor.cpp](14_item_visitor.cpp) | Visit one item | A named callback counts array-entry events without building a decoded value. |
| [15_typed_array.cpp](15_typed_array.cpp) | Opt into typed arrays | Signed 32-bit elements use RFC 8746 tag 78 and a little-endian byte-string payload. |
| [16_unique_pointer.cpp](16_unique_pointer.cpp) | Opt into unique pointers | A pointee encodes as its value; an empty pointer encodes null. |
| [17_custom_codec.cpp](17_custom_codec.cpp) | Write an application codec | An explicit codec represents Temperature as one integer rather than an aggregate array. |
| [18_cddl_schema.cpp](18_cddl_schema.cpp) | Generate a schema | Generate and print the same record schema introduced in example 04. |

## What to expect

Successful programs return zero. An encode/decode failure returns 1; a wrong
result or an unexpected status returns 2. Example 18 also prints:

```text
record = [int, tstr]
```

Example 02 shows why the sender and receiver do not need identical C++ types:
CBOR describes the value on the wire. A `std::string` and a `std::string_view`
both represent a text string. The receiving view borrows the encoded buffer,
which must stay alive and unchanged while the view is used. Other destination
constraints still apply: example 10 shows that a matching integer major type
alone does not guarantee representability.

The CDDL describes the encoded data, including error-example inputs. Parentheses
describe a sequence of items; brackets describe one array. CDDL describes array
contents without choosing definite or indefinite encoding. Example 11 selects
the indefinite encoding in C++.

## Build and run

Use a compatible C++20 compiler, CMake 3.23 or newer, and Ninja. The existing
CMake dependency setup supplies fmt and nameof. No OpenSSL dependency is needed.
The first fourteen examples use core APIs; examples 15–17 explicitly install
codec mixins, and example 18 includes the CDDL extension.

```bash
cmake -S . -B build/examples -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_STANDARD=20 \
  -DCMAKE_CXX_EXTENSIONS=OFF \
  -DCBOR_TAGS_BUILD_EXAMPLES=ON \
  -DCBOR_TAGS_BUILD_TESTS=OFF \
  -DCBOR_TAGS_BUILD_CLI=OFF
cmake --build build/examples --parallel
ctest --test-dir build/examples -L examples --output-on-failure --no-tests=error
```

Run one program directly:

```bash
./build/examples/examples/cbor_tags_example_01_basic_values
```

For a multi-configuration generator, build with `--config Debug`, test with
`-C Debug`, and use the executable in the `examples/Debug` directory. Windows
executables have the `.exe` suffix. Set `CMAKE_BUILD_TYPE=Release` for a Release
build; the examples' explicit checks remain active.

## Advanced reference helpers

[bounded_samples.h](bounded_samples.h) and
[cxx26_value_wrappers.h](cxx26_value_wrappers.h) remain separately tested helpers
for the detailed codec documentation. They are not part of this introductory
sequence.
