# Status errors

Encoder and decoder call operators return an expected result. Check success
before reading `.error()`; `status_message(code)` provides a diagnostic.

```cpp
std::vector<std::byte> input{std::byte{0x1c}};
std::uint64_t value{};
auto result = cbor::tags::make_decoder(input)(value);
assert(!result);
assert(result.error() == cbor::tags::status_code::invalid_additional_info);
```

Here `0x1c` is an integer header with reserved additional information. Replacing
it with `0x18` gives `incomplete`: that valid header needs one more byte.
Both failures are terminal for the decoder instance; neither permits resuming
with more input. See the [decoder contract](decoder_resource_limits.md#decoder-contract).

## Classifications

| Status | Meaning and example |
| --- | --- |
| `invalid_additional_info` | Reserved additional information 28–30, or 31 where an argument is required, such as an integer or tag. |
| `malformed_structure` | A structural violation detected while decoding an item: a text chunk inside a byte string, nested indefinite string chunk, misplaced break, or a map ending without a value. Strict traversal also uses this for the forbidden two-byte encoding of a simple value below 32. |
| `input_output_aliasing` | The existing mutable string storage check detected overlap with decoder input. |
| `unsupported_operation` | An `as_indefinite` wrapper targets an unsupported type, such as a scalar. |
| `size_limit_exceeded` | An explicit bound, input-size representation, scanner item count, or scanner depth limit is exceeded. |
| `unexpected_group_size` | A fixed extent or group size does not match, including an indefinite header where an exact header size was requested. |
| `incomplete` | The admitted input ends before the requested segment is available. |
| `out_of_memory` | `std::bad_alloc` or `std::length_error`, including existing destination capacity checks. |
| `no_match_for_*` | The input does not match the requested type or tag. Variant dispatch may try another compatible alternative. |
| `error` | A failure without a more specific classification; see below. |

These statuses describe the operation actually attempted. Typed decoding does
not validate arbitrary trailing data or become a structural scanner. A wrong
major type can be rejected before its argument is inspected. Raw views and
lazy tag discovery use their existing scanners; traversal has its own strict
validation option. This change does not broaden the accepted CBOR encodings.

Value-returning decoder primitives carry a typed internal exception to the
nearest result boundary. Status-returning helpers propagate it directly.
The implementation does not inspect exception message text. Direct low-level
primitive calls retain their `std::runtime_error` exception category; codec users
should normally return the result
of the decoder call operator.

## Compatibility note

The four new enum members are appended after `size_limit_exceeded`. Existing
numeric values, `uint8_t` storage, public result types, and the variant mismatch
interval are unchanged. The new statuses are terminal during variant dispatch.

Code that treated only `status_code::error` as failure must instead check the
expected result or handle the refined statuses above. Invalid indefinite chunks
and missing map values now return `malformed_structure` instead of a retriable
major mismatch. Wrong fixed array/map headers return their existing major
mismatch status. Scanner depth exhaustion now returns `size_limit_exceeded`.
RFC 8746 payload views that discard bytes return `unexpected_group_size`.

Input ownership, one-pass unsized decoding, allocation/reservation rules, and
retention of a decoded prefix on failure are unchanged. The core decoder still
catches standard exceptions only; traversal retains its existing catch-all.

## Retained generic paths

The source audit deliberately retains `error` at these boundaries:

- Decoder, encoder, segment, and traversal exception fallbacks: unknown
  application codec, allocator, range, or visitor exceptions. Encoder API
  misuse and exhausted fixed output buffers also keep their existing fallback.
- Decoder compile-time rejection branches: their preceding `static_assert`
  makes the return unreachable. The raw item view passes an unused mismatch
  argument when no expected major type was supplied.
- `detail/cbor_item.h`: impossible major/frame states and inconsistent iterator
  ordering. `detail/cbor_raw_view_decode.h` likewise retains its negative-distance
  guard for an inconsistent range.
- Smart pointer extensions: unsupported graph state, invalid reference index or
  pointee type, and compile-time rejection branches.
- `std_indirect`: a valueless encoding source and a compile-time rejection branch.
- `detail/custom_codec_1_serialization.h`: its separate payload format retains
  its current error policy. Its CBOR envelope uses the refined core statuses.

These paths do not claim a CBOR parse diagnosis that the library cannot establish.
