# Decoder Resource Limits

The synchronous typed decoder reads CBOR directly. It does not perform a structural preflight pass or impose a nesting limit. Apply an
application message-size limit before decoding:

```cpp
if (input.size() > max_message_bytes)
    return input_too_large;

return cbor::tags::make_decoder(input)(value);
```

For streaming or resumable parsing, enforce the same policy against the cumulative bytes received.

## Decoder Contract

The core CBOR decoder accumulates into mutable owning sequence destinations.
Arrays, maps, byte strings, and text strings are appended to or inserted into;
they are not cleared first. Initialize a destination empty when replacement
semantics are wanted. Codec extensions can define a different destination
contract.

```cpp
std::string source = "payload";
std::vector<std::byte> buffer;
auto enc = make_encoder(buffer);
enc(source);

std::string destination = "prefix:";
auto dec = make_decoder(buffer);
dec(destination);
assert(destination == "prefix:payload");
```

A failed encoder or decoder call is terminal for that instance. Its destination
or output buffer may be partially modified; discard it after failure unless its
type documents a stronger guarantee. In particular, `status_code::incomplete`
means that the fixed input does not contain a complete value and does not make
the decoder resumable. The caller owns the input buffer; the next section
defines the input and segment contract.

Definite borrowed views are formed only after their complete payload is
available, preventing out-of-bounds views without a second traversal.

Mutable owning text- and byte-string destinations whose exposed contiguous
storage overlaps the decoder input are rejected with `status_code::error`; use
separate input and output storage. Other mutable output types must not alias
the decoder input: the runtime check is not general alias analysis. Input range
adaptors and views that hide shared storage are unsupported and must also use
separate storage. Fixed-size destinations and borrowed views retain their
exact-size or assignment semantics. Advanced callers that can guarantee
separate storage can disable the runtime check through
`unchecked_aliasing_decoder_options`; see [encoder and decoder
options](options.md#unchecked-inputoutput-aliasing).

While encoding, input values must not alias an appendable output buffer. A fixed
output span may share its backing allocation with a source span only when their
byte regions do not overlap; see [encoder source/output
aliasing](options.md#encoder-sourceoutput-aliasing).

## Input Buffer And Segment Parsing

The ownership split is deliberately simple:

- **Input buffer:** caller responsibility. The caller owns its lifetime,
  framing/admission limit, and a stable admitted `[begin, end)` range for the
  call. The caller also owns the truthfulness of the C++ range semantics it
  exposes, including any `std::ranges::sized_range` extent promise.
- **Parsing a given CBOR segment from that buffer:** library responsibility.
  Here, a segment is the next requested CBOR item (or items), including a
  definite item's header-declared payload. It is not a transport frame or a
  promise to validate arbitrary trailing input. The decoder accepts supported
  input ranges that do not model `std::ranges::sized_range`.

An unsized non-contiguous input is not a request for a structural preflight
pass. When a CBOR header declares a definite byte/text-string payload or
container length, the core typed decoder consumes that segment once. If it
reaches the end before the declared segment is complete, it returns
`status_code::incomplete`; it does not rewind the reader or undo already
decoded destination contents.

```cpp
// `input` is a supported non-contiguous range that does not model
// std::ranges::sized_range.
// It contains: 0x65, 'o', 'k' -- a tstr header that declares five bytes.
std::string output{"prefix:"};
const auto result = cbor::tags::make_decoder(input)(output);

// result.error() == cbor::tags::status_code::incomplete
// output == "prefix:ok"; the input has been consumed to its end.
```

This is a one-shot decoder contract, not a resumable or transactional one. A
caller that needs message-size admission checks performs them at its transport
or framing boundary. A caller that supplies a `std::ranges::sized_range` is
responsible for that range's size/extent contract.

For contiguous input or input that models `std::ranges::sized_range`, the
decoder uses the range-provided extent to check a definite string payload's
exact availability before reserving, without an explicit decoder prewalk. For
definite arrays/maps, it performs only a lower-bound reservation guard: one
input byte per array item and two input bytes per map entry. That guard does
not prove a complete or structurally valid container. For unsized
non-contiguous input, the core decoder deliberately does neither a validation
traversal nor a header-directed reservation: a CBOR length header alone never
justifies `reserve`.

This section describes ordinary core typed decoding. An extension may define
its own documented terminal availability/status boundary, but that does not
authorize a prewalk for recovery, ordinary decode validation, or rollback. A
scanner is a separately designed semantic feature (for example, CDDL or
lazy-tag discovery), not a generic extension escape hatch. Such a feature
needs its own documentation and tests, returns an explicit terminal status,
and does not make prewalking or rollback valid in the core decoder.

## Bounded Objects, PMR, And CDDL

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

#include <cbor_tags/cbor_decoder.h>

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

### CDDL For Bounded Objects

A PMR allocator is runtime state, so it does not change the CBOR or CDDL shape.
When a bound belongs to the protocol, put a static `bounded_size` member in the
object type. The same type then validates the field and describes it in CDDL:

```cpp
#include <cbor_tags/extensions/cbor_visualization.h>

#include <cstdint>
#include <memory_resource>
#include <string>
#include <vector>

namespace ct = cbor::tags;

struct bounded_request {
    ct::bounded_size<std::pmr::string, 1, 64> name;
    ct::bounded_size<std::pmr::vector<std::uint64_t>, 0, 8> samples;
};

fmt::memory_buffer schema;
ct::cddl_schema_to<bounded_request>(
    schema,
    {.row_options = {.format_by_rows = false}});
// bounded_request = [tstr .size (1..64), [0*8 uint]]
```

Use `dynamic_bounded_size` or `as_bounded_size(value, min, max)` when limits
come from application configuration. Those limits are instance data, so they
cannot generate type-based CDDL. See [CDDL Size-Bounded
Containers](cddl_handling.md#size-bounded-containers) for nested fields and
range-wrapper examples.

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

For an input that models `std::ranges::sized_range`, definite container lengths
receive a range-provided lower-bound check before `reserve`: one byte per array
item and two bytes per map pair. This caps a header-directed reservation by
remaining input bytes; it is not a full structural validation. For an unsized
input, the decoder does not walk the remaining range merely to validate an
untrusted length or justify reservation; it decodes incrementally and may
retain a successfully decoded prefix on failure. Indefinite containers also
decode in one pass and can retain a successfully decoded prefix when a later
item exceeds the bound. See
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
