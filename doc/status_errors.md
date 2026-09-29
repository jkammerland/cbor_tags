# Status errors

Encoder and decoder call operators return an expected result. Check it before
reading `.error()`. Use `status_message(code)` for a diagnostic:

```cpp
#include <cbor_tags/cbor_decoder.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace ct = cbor::tags;

bool read_count(const std::vector<std::byte> &input, std::uint64_t &count) {
    const auto result = ct::make_decoder(input)(count);
    if (!result) {
        std::cerr << ct::status_message(result.error()) << '\n';
        return false;
    }
    return true;
}
```

Handle every failed result. `status_code::error` is the fallback when no more
specific status describes the failure.

## Invalid or incomplete input

`invalid_additional_info` means a CBOR header uses an invalid argument encoding.
`incomplete` means the input ends before the requested item is complete:

```cpp
std::uint64_t value{};

// 0x1c is an integer header with reserved additional information.
const std::vector<std::byte> invalid{std::byte{0x1c}};
const auto invalid_result = ct::make_decoder(invalid)(value);
assert(!invalid_result);
assert(invalid_result.error() == ct::status_code::invalid_additional_info);

// 0x18 is a valid integer header, but its argument byte is missing.
const std::vector<std::byte> truncated{std::byte{0x18}};
const auto truncated_result = ct::make_decoder(truncated)(value);
assert(!truncated_result);
assert(truncated_result.error() == ct::status_code::incomplete);
```

A failed call is terminal for that encoder or decoder instance. Even
`incomplete` requires a fresh decoder with complete input to try again.

## Partial output after failure

Failure can leave a decoded prefix in the destination. Discard failed output
unless its type documents a stronger guarantee. For example, a text string
containing a byte-string chunk returns `malformed_structure`:

```cpp
const std::vector<std::byte> input{
    std::byte{0x7f},                                  // Indefinite text string.
    std::byte{0x62}, std::byte{'o'}, std::byte{'k'},     // Text chunk: "ok".
    std::byte{0x41}, std::byte{0x00},                   // Invalid byte-string chunk.
    std::byte{0xff},                                  // Break.
};
std::string text;
const auto result = ct::make_decoder(input)(text);
assert(!result);
assert(result.error() == ct::status_code::malformed_structure);
assert(text == "ok"); // Partial output remains after failure.
```

See the [decoder contract](decoder_resource_limits.md#decoder-contract) for
input ownership and destination behavior.

## Other common statuses

| Status | Meaning |
| --- | --- |
| `input_output_aliasing` | Detected overlap between input and a mutable text- or byte-string destination. Use separate storage. |
| `unsupported_operation` | The requested operation is unsupported for the selected type, such as `as_indefinite` on an integer. |
| `size_limit_exceeded` | An item exceeds a size or depth limit for the requested operation. |
| `unexpected_group_size` | An array, map, or payload has the wrong size for a fixed-size destination. |
| `out_of_memory` | Allocation failed or the destination cannot hold the requested size. |
| `no_match_for_*` | The input does not match the requested type or tag. A variant may try another compatible alternative. |

Malformed input stops variant decoding. These results describe the requested
item; successful typed decoding does not validate trailing input.
