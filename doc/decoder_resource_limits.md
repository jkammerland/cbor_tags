# Decoder Resource Limits

The synchronous typed decoder reads CBOR directly. It does not perform a structural preflight pass or impose a nesting limit. Apply an
application message-size limit before decoding:

```cpp
if (input.size() > max_message_bytes)
    return input_too_large;

return cbor::tags::make_decoder(input)(value);
```

For streaming or resumable parsing, enforce the same policy against the cumulative bytes received.

## Input Buffer And Segment Parsing

The ownership split is deliberately simple:

- **Input buffer:** caller responsibility. The caller owns its lifetime,
  framing/admission limit, total extent, and any size/extent promise exposed by
  the range.
- **Parsing a given CBOR segment from that buffer:** library responsibility.
  The decoder accepts supported input ranges that do not expose `size()`.

An unsized input is not a request for a structural preflight pass. When a CBOR
header declares a definite byte/text-string payload or container length, the
decoder consumes that segment once. If it reaches the end before the declared
segment is complete, it returns `status_code::incomplete`; it does not rewind
the reader or undo already decoded destination contents.

```cpp
// `input` is a supported non-contiguous range with no size/extent operation.
// It contains: 0x65, 'o', 'k' -- a tstr header that declares five bytes.
std::string output{"prefix:"};
const auto result = cbor::tags::make_decoder(input)(output);

// result.error() == cbor::tags::status_code::incomplete
// output == "prefix:ok"; the input has been consumed to its end.
```

This is a one-shot decoder contract, not a resumable or transactional one. A
caller that needs message-size admission checks performs them at its transport
or framing boundary. A caller that supplies a sized range is responsible for
that range's size/extent contract.

For contiguous or sized input, the decoder can confirm that a declared payload
fits in O(1), then reserve storage where appropriate. For unsized
non-contiguous input, it deliberately does neither a validation traversal nor
a header-directed reservation: a CBOR length header alone never justifies
`reserve`.

## Container And Allocation Limits

Plain owning containers do not impose protocol limits. A transport-level byte
limit does not express per-field limits for arrays, maps, text strings, or byte
strings.

Use `cbor::tags::bounded_size<T, Min, Max>` for type-level bounds and
`cbor::tags::as_bounded_size(value, min, max)` for limits selected at runtime.
Use allocator-aware containers backed by a bounded `std::pmr::memory_resource`
when decoded allocations also need a hard ceiling.

These controls are complementary:

- A message-size limit bounds accepted input bytes.
- A size wrapper validates one CBOR item against protocol limits.
- A bounded PMR resource contains allocations made while materializing values.

### Bounded PMR Example

```cpp
#include <array>
#include <cstddef>
#include <memory_resource>
#include <string>
#include <vector>

#include <cbor_tags/cbor.h>

namespace ct = cbor::tags;

// Example input: ["a", "b"].
std::vector<std::byte> input{
    std::byte{0x82}, std::byte{0x61}, std::byte{'a'},
    std::byte{0x61}, std::byte{'b'},
};

std::array<std::byte, 4096> arena_storage{};
std::pmr::monotonic_buffer_resource arena(
    arena_storage.data(),
    arena_storage.size(),
    std::pmr::null_memory_resource());

std::pmr::vector<std::pmr::string> values{&arena};

auto dec = ct::make_decoder(input);
std::size_t max_items = 64;
auto result = dec(ct::as_bounded_size(values, 0, max_items));

if (!result && result.error() == ct::status_code::out_of_memory) {
    // The bounded arena was exhausted.
}
```

The same approach works for nested PMR-aware layouts, including:

```cpp
std::pmr::vector<std::pmr::string>
std::pmr::vector<std::pmr::vector<int>>
std::pmr::map<std::pmr::string, std::pmr::string>
std::pmr::vector<std::optional<std::pmr::string>>
```

The arena must use a bounded upstream resource such as
`std::pmr::null_memory_resource()`. PMR contains allocations; it does not perform
schema validation or limit how much input the application accepts.

### Size-Bound Behavior

- Static and runtime bounds validate only the immediately wrapped field. Wrap
  nested containers separately when they also require limits.
- A bound constrains one incoming or outgoing CBOR item, not the accumulated size
  of a pre-populated destination. Decoding appends to owning containers.
- `bounded_size<T, Min, Max>` generates matching type-based CDDL.
  `dynamic_bounded_size<T>` contains instance data and cannot produce type-based
  CDDL.
- Runtime-bounded destinations must already contain their bounds. Generic
  variant, optional, and container decoding cannot invent bounds for values they
  create.
- RFC 8746 scalar typed arrays interpret static and runtime bounds as element
  counts. Static bounds generate corresponding CDDL byte-string constraints.
- `std::variant` alternatives do not currently receive their parent's PMR
  allocator context.

For a sized input, definite container lengths receive a constant-time lower
bound check before `reserve`: one byte per array item and two bytes per map
pair. This caps a header-directed reservation by remaining input bytes; it is
not a full structural validation. For an unsized input, the decoder does not
walk the remaining range merely to validate an untrusted length or justify
reservation; it decodes incrementally and may retain a successfully decoded
prefix on failure. Indefinite containers also decode in one pass and can retain
a successfully decoded prefix when a later item exceeds the bound. See
[CDDL Size-Bounded Containers](cddl_handling.md#size-bounded-containers) for
nesting and range-wrapper examples.

## Recursive Decode Paths

Typed decoding follows the destination C++ type. Container elements are processed in loops, so a large flat array does not create one
decoder call frame per element. Extra input nesting against a fixed, non-recursive destination instead produces a type mismatch.

Input-controlled stack growth requires a decode path that can invoke itself. The usual trigger is a recursively defined destination type:

```cpp
struct recursive_array {
    std::vector<recursive_array> children;

    template <typename Decoder>
    cbor::tags::expected<void, cbor::tags::status_code> decode(Decoder& decoder) {
        return decoder(children);
    }
};
```

Each child can decode another `recursive_array`, so nested one-element arrays add live C++ call frames. Mutually recursive destination
types, recursive smart-pointer graphs, and custom codecs that explicitly call back into a recursive decode path can do the same.

## Measured Boundary

These are observations from one machine, not portable decoder limits:

- Source revision: `6617765`
- Host: x86-64 Linux with an 8 MiB main-thread stack
- GCC 15.2.1 and Clang 21.1.8 with libstdc++ 15, C++20
- Address randomization disabled for repeatable boundary measurements

The probe decoded the `recursive_array` above from `D` one-element array headers (`0x81`) followed by an empty array (`0x80`). Its
buffer size was therefore `D + 1` bytes.

| Compiler configuration | Maximum successful depth | First failing depth | Buffer at first failure | Approx. stack per level |
|---|---:|---:|---:|---:|
| GCC 15.2.1, `-O0 -g` | 14,975 | 14,976 | 14,977 bytes | 560 bytes |
| GCC 15.2.1, `-O3 -DNDEBUG` | 52,421 | 52,422 | 52,423 bytes | 160 bytes |
| Clang 21.1.8, `-O0 -g` | 13,103 | 13,104 | 13,105 bytes | 640 bytes |
| Clang 21.1.8, `-O3 -DNDEBUG` | 87,369 | 87,370 | 87,371 bytes | 96 bytes |

The smallest observed buffer to exhaust the 8 MiB stack was 13,105 bytes. This boundary varies with the destination type, custom codec,
compiler, optimization level, standard library, caller stack use, and thread stack size.

[`test_decode_stack_floor.cc`](../test/test_decode_stack_floor.cc) checks the same recursive path at depth 1,024 as a regression floor;
it does not assert a portable crash boundary.
