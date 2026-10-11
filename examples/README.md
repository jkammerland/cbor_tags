# Learn cbor_tags one step at a time

Read the numbered programs in order. Each is a standalone C++20 program, with
its CDDL immediately above `main()` and short comments separating the steps.
Examples 01 and 03 group related basics; later files introduce one feature each.
Examples 19–22 form an experimental range, view and segment sequence.
Example 23 introduces experimental CWT claim encoding and decoding; example 24
connects an application crypto backend using optional OpenSSL.
Example 25 builds a complete signed token with external AAD and receiver validation;
example 26 requires signatures from two independent keys.
Example 27 chooses application layouts; examples 28–29 keep payload bytes borrowed
through signing, token encoding, decoding and verification.

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
| [14_item_visitor.cpp](14_item_visitor.cpp) | Visit one item | Print value bytes at their nesting depth, separating groups and omitting container headers. |
| [15_typed_array.cpp](15_typed_array.cpp) | Opt into typed arrays | Signed 32-bit elements use RFC 8746 tag 78 and a little-endian byte-string payload. |
| [16_unique_pointer.cpp](16_unique_pointer.cpp) | Opt into unique pointers | A pointee encodes as its value; an empty pointer encodes null. |
| [17_custom_codec.cpp](17_custom_codec.cpp) | Write an application codec | An explicit codec represents Temperature as one integer rather than an aggregate array. |
| [18_cddl_schema.cpp](18_cddl_schema.cpp) | Generate a schema | Generate and print the same record schema introduced in example 04. |
| [19_experimental_ranges.cpp](19_experimental_ranges.cpp) | Encode a filtered range | Avoid an intermediate container while still writing CBOR bytes. |
| [20_experimental_byte_view.cpp](20_experimental_byte_view.cpp) | Borrow a decoded byte payload | A decoded span points into the encoded input rather than copying its payload. |
| [21_experimental_segments.cpp](21_experimental_segments.cpp) | Encode headers and borrowed payloads | Keep headers in owned segments and views into original text and bytes in wire order. |
| [22_experimental_forward_view.cpp](22_experimental_forward_view.cpp) | Forward an encoded view | Borrow a complete encoded array without decoding or re-encoding its elements. |
| [23_experimental_cwt_claims.cpp](23_experimental_cwt_claims.cpp) | Encode and decode CWT claims | Opt into registered integer claim labels; signing and application validation are separate steps. |
| [24_experimental_cwt_backend.cpp](24_experimental_cwt_backend.cpp) | Connect a custom crypto backend | Forward an application key through signing and verification, using OpenSSL as the replaceable provider. |
| [25_experimental_cwt_token.cpp](25_experimental_cwt_token.cpp) | Send and verify a complete CWT | Protect headers, supply external AAD, encode and decode the tagged message, then validate the verified claims. |
| [26_experimental_cwt_multi_signer.cpp](26_experimental_cwt_multi_signer.cpp) | Require two token signers | Append a COSE_Sign signature and verify each signer with its own trusted key. |
| [27_experimental_cwt_custom_types.cpp](27_experimental_cwt_custom_types.cpp) | Choose application field names and storage | Encode an owning application type and decode into a different type with borrowed text. |
| [28_experimental_cwt_borrowed_token.cpp](28_experimental_cwt_borrowed_token.cpp) | Sign and receive through views | Use a caller-owned signature buffer, encode segments, then decode views, verify with AAD and check claims. |
| [29_experimental_cwt_borrowed_multi_signer.cpp](29_experimental_cwt_borrowed_multi_signer.cpp) | Share one payload between signers | Keep separate signature buffers and require both trusted signers before reading borrowed claims. |
| [30_experimental_cwt_fields.cpp](30_experimental_cwt_fields.cpp) | Select application fields | Integer and text labels bind to independently chosen receiver storage. |
| [31_experimental_cwt_type.cpp](31_experimental_cwt_type.cpp) | Describe the complete token | Protected `typ` distinguishes a COSE object from its payload content type. |
| [32_experimental_cwt_header_claims.cpp](32_experimental_cwt_header_claims.cpp) | Carry claims in protected headers | Verify first, then compare replicated claims by value. |
| [36_cwt_public_keys.cpp](36_cwt_public_keys.cpp) | Encode public key material | COSE_Key represents public parameters independently of a crypto library. |

## CDDL

CDDL describes the structure of CBOR data, such as a record containing an integer
and a text string:

```text
record = [int, tstr]
```

See the [CDDL standard, RFC 8610](https://www.rfc-editor.org/rfc/rfc8610.html).

## Build and run

From the repository root, use a C++20 compiler, CMake 3.23 or newer, and Ninja:

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

Run one example directly:

```bash
./build/examples/examples/cbor_tags_example_01_basic_values
```

For multi-configuration generators, build with `--config Debug`, test with
`-C Debug`, and run the executable from `examples/Debug`; Windows adds `.exe`.

Examples 24–26, 28–29, and 32 additionally require OpenSSL development files and the explicit opt-in:

```bash
cmake -S . -B build/examples -DCBOR_TAGS_ENABLE_CWT_OPENSSL=ON
cmake --build build/examples --target \
  cbor_tags_example_24_experimental_cwt_backend \
  cbor_tags_example_25_experimental_cwt_token \
  cbor_tags_example_26_experimental_cwt_multi_signer \
  cbor_tags_example_28_experimental_cwt_borrowed_token \
  cbor_tags_example_29_experimental_cwt_borrowed_multi_signer
ctest --test-dir build/examples -R '^example_(24|25|26|28|29)_' --output-on-failure --no-tests=error
```
