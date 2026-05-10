# Performance Regression And Zero-Copy Range Plan

## Purpose

This document is the working plan for investigating performance regressions
since `v0.10.0` and for designing the next range/raw-view/zero-copy feature
set. It is intentionally a plan document, not a claim that the described APIs
already exist.

The goals are:

- identify and attribute regressions introduced after `v0.10.0`
- restore efficient byte-string append paths for direct and range-based APIs
- support raw encoded CBOR array/map/item views that can be re-encoded or
  embedded in byte strings
- define a segmented output model for true zero-copy payload emission
- handle both definite and indefinite CBOR without normalizing wire shape unless
  the caller asks for it

## Current Observations

`v0.10.0` is the comparison baseline. Later releases and feature branches added
C++26 named maps, CDDL coverage, range wrappers, lazy tag scanning, benchmark
work, and performance recovery commits. Those changes should be measured as
separate buckets instead of treated as one large regression.

The current `as_bstr_range` implementation has a generic byte-wise path for
sized ranges:

```cpp
enc.encode_major_and_size(size, 0x40);
for (auto byte : range) {
    enc.appender_(enc.data_, output_byte(byte));
}
```

This is correct for arbitrary views, but it is unnecessarily slow for sized
contiguous byte ranges. Direct binary-string encode still has access to
`appender_(data_, value)`, which can use bulk insert or `memcpy` for supported
destinations.

Recent benchmark rows show three useful levels for RFC 8746 typed arrays:

- range path: existing byte-view generation and byte-wise append
- bulk-copy path: materialize a normal contiguous CBOR buffer with one payload
  append
- segment path: represent `tag + bstr header` and borrowed raw payload as
  separate segments

The segment path is a zero-copy ceiling, not a standalone `std::vector` result.

## Regression Investigation Plan

Compare these refs in isolated worktrees:

- `v0.10.0`
- `v0.14.0`
- `master`
- `origin/feat/range-support-v2`
- current working branch

Use identical build settings:

- Release build
- same compiler and standard library
- same CMake options where supported
- fixed benchmark payloads
- no `std::random_device` in measured payload setup
- no doctest `CHECK(...)` rows in timing measurements
- cold and reserved output-buffer paths measured separately

Measure these groups:

- primitive encode/decode: uint, nint, bool, null, float16, float32, float64,
  tag
- direct bstr/tstr encode/decode, small and large
- direct arrays/maps and tagged structs
- variant and optional decode paths
- C++26 named-map paths where available
- range wrappers on the current branch
- lazy tag scanning on the current branch
- raw encoded view paths once implemented

For each row, record:

- encoded bytes
- ns/op or ns/byte
- cold encode allocations
- reserved encode allocations
- decode allocations
- first suspected commit range
- whether the regression is semantic, structural, allocation-related, or
  benchmark-artifact-related

Classification thresholds:

- `>5%`: record as notable
- `>15%`: candidate for a fix
- any new allocation on reserved encode: blocker unless justified
- any extra decode allocation on a view/borrowed path: blocker unless justified

The output should be a Markdown report plus raw JSON/CSV so later benchmark runs
can be compared mechanically.

## Efficient Append Strategy

Introduce an internal append strategy used by direct bstr/tstr encode,
`as_bstr_range`, raw encoded views, and future typed-array helpers.

The strategy should select the widest safe operation for the source and output:

| Source | Output | Expected strategy |
|---|---|---|
| contiguous, sized byte range | contiguous fixed buffer | bounds check once, then `memcpy` |
| contiguous, sized byte range | appendable contiguous container | range insert or bulk append |
| contiguous, sized byte range | non-contiguous output | destination range insert if available, otherwise chunked append |
| non-contiguous, sized byte range | contiguous output | reserve final size when possible, then append by chunks |
| non-contiguous, sized byte range | non-contiguous output | append by source chunks if discoverable, otherwise element fallback |
| input-only or transform/filter byte range | any output | current byte-wise fallback |

The direct bstr/tstr path should stay fast. The range path should become fast
only when the range properties make that safe.

### Definite Byte Strings

For sized byte ranges:

1. Emit a definite bstr header.
2. Append payload through the append strategy.
3. Preserve the exact byte values.
4. Avoid allocation when the caller has already reserved enough destination
   storage.

### Indefinite Byte Strings

For non-sized byte ranges:

1. Emit the indefinite bstr start byte.
2. Accumulate chunks up to `chunk_size`.
3. Emit each chunk as a definite bstr.
4. Flush each chunk through the same append strategy.
5. Emit the break byte.

For sized-but-explicitly-chunked future APIs, use the same chunked strategy but
do not require normalizing to one definite bstr.

Important constraint: indefinite bstr chunks are separate CBOR byte strings.
The implementation may optimize each chunk append, but it must not emit raw
payload bytes without chunk headers.

## Raw Encoded View Feature

Add borrowed raw encoded views:

- `encoded_item_view`
- `encoded_array_view`
- `encoded_map_view`

These views represent already-encoded CBOR bytes borrowed from an input buffer.
They do not decode child elements.

Decode behavior:

- validate and skip exactly one CBOR item
- borrow the original encoded byte range
- fail if the requested major type does not match
- fail cleanly on malformed or truncated input
- preserve original definite or indefinite wire form

Indefinite handling:

- raw arrays/maps include the start byte, all child items, and the final break
- nested indefinite arrays/maps/bstr/tstr are validated and included
- break bytes outside indefinite containers are invalid
- unterminated indefinite containers are rejected
- indefinite strings must contain only definite chunks of the same string major
  type

Encode behavior:

- `as_encoded_item(view)` appends the borrowed encoded bytes directly
- `encoded_array_view` and `encoded_map_view` encode through the same raw item
  mechanism
- output uses the efficient append strategy
- no normalization from indefinite to definite is performed

Embedding in byte strings:

```cpp
encoded_array_view raw_array;
dec(raw_array);

enc(as_bstr_range(raw_array.bytes()));
```

This produces a bstr containing encoded CBOR bytes. A later convenience helper
may add tag-24 encoded-CBOR behavior, but that is separate from the raw view
mechanism.

## Segmented Zero-Copy Output

A single `std::vector` output cannot be truly zero-copy when the payload lives
elsewhere. True zero-copy encode requires a segmented output representation.

Add a segmented output model with:

- small owned header segment storage
- borrowed payload segments
- optional repeated segments for chunked indefinite output
- a flattening helper for tests and for users that need a contiguous buffer
- explicit lifetime rules for borrowed payloads

Typed-array and raw-view examples:

```cpp
auto encoded = encode_segments(as_typed_array(values));

// Conceptual shape:
// segment 0: CBOR tag + bstr header
// segment 1: borrowed span over values bytes
```

For indefinite/chunked output:

```cpp
// Conceptual shape:
// segment 0: 0x5f
// segment 1: chunk bstr header
// segment 2: borrowed/copy chunk payload
// ...
// final segment: 0xff
```

Segmented output must make ownership explicit. A segment that borrows from a
temporary range must be rejected or impossible to construct safely.

## Typed Array Direction

Typed arrays should be built on top of raw byte views and segmented output, not
hidden inside plain `as_bstr_range`.

Initial helpers should support RFC 8746 little-endian tags used in the
benchmarks:

- tag `78`: signed int32 little-endian
- tag `79`: signed int64 little-endian
- tag `86`: float64 little-endian

Encoding modes:

- contiguous CBOR buffer with bulk-copy payload append
- segmented output with header and borrowed payload
- generic byte-range fallback when conversion is required

Decode modes:

- byte-span payload view
- safe unaligned typed view
- aligned `std::span<const T>` only when payload offset and alignment make that
  legal
- owning `std::vector<T>` fallback

Typed-array decode must validate:

- expected tag
- bstr major type
- payload length is a multiple of element width
- no trailing bytes when decoding a single item
- endian policy is explicit

## Implementation Sequence

Current branch status:

- steps 2-4 are implemented by the append-strategy helpers used by
  `as_bstr_range`, including bulk appends for contiguous sized byte ranges and
  chunk flushes for indefinite byte-string ranges
- steps 5-6 are implemented by `cbor_raw_views.h` with
  `encoded_item_view`, `encoded_array_view`, `encoded_map_view`, and typed
  `encoded_*_view_for<Buffer>` aliases for non-contiguous buffers
- step 7 is implemented by opt-in `cbor_segments.h` helpers for definite,
  indefinite, tagged, and raw-view segmented output
- step 8 is implemented by opt-in `extensions/rfc8746_typed_arrays.h` helpers
  for RFC 8746 little-endian int32, int64, float16, float32, and float64 arrays

Remaining work:

- complete the historical benchmark/regression attribution against `v0.10.0`
- decide whether the new opt-in APIs should be re-exported by broader public
  headers or remain split-header-only
- extend typed-array support beyond the initial benchmark tags only if there is
  a concrete user need

1. Benchmark and report regressions from `v0.10.0`.
2. Add append-strategy helpers behind existing encoder APIs.
3. Optimize direct bstr/tstr and contiguous `as_bstr_range` without changing
   public API.
4. Optimize chunk flushes for indefinite `as_bstr_range`.
5. Add raw encoded item/array/map view types and tests.
6. Add raw view re-encode and bstr embedding support.
7. Add segmented output model and flattening tests.
8. Add typed-array helpers using bulk-copy and segment paths.
9. Update public docs once APIs stabilize.

Each implementation step should be committed separately with benchmark evidence
when it affects performance.

## Tests

Regression report tests:

- benchmark rows use fixed payloads
- reports include sizes, timings, and allocation counts
- cold and reserved encode paths are separate

Append strategy tests:

- contiguous byte source to `std::vector` output uses bulk append
- contiguous byte source to fixed output buffer uses one checked copy
- contiguous byte source to `std::deque` output preserves bytes
- non-contiguous sized source to vector output preserves bytes
- transform/filter byte ranges preserve current fallback behavior
- caller-reserved definite bstr encode performs no additional allocations

Indefinite bstr tests:

- non-sized byte ranges produce valid chunked indefinite bstr
- chunk size zero is rejected
- chunk payloads are emitted with definite bstr headers
- chunk flushing avoids avoidable allocations
- non-contiguous chunk sources preserve exact bytes

Raw view tests:

- decode definite and indefinite raw items
- decode definite and indefinite raw arrays/maps
- preserve original encoded bytes byte-for-byte
- reject wrong major type for raw array/map views
- reject truncated definite payloads
- reject invalid break bytes
- reject unterminated indefinite containers
- re-encode raw views byte-for-byte
- embed raw view bytes in bstr and decode the bstr back

Segmented output tests:

- flattening segmented output matches contiguous output byte-for-byte
- segment encode has zero payload allocations
- borrowed segment payload pointers reference the original input
- temporary or non-borrowable payloads cannot produce unsafe segments
- indefinite segmented output flattens to valid CBOR

Typed-array tests:

- encode/decode int32, int64, and float64 typed arrays
- validate wrong tag rejection
- validate payload length mismatch rejection
- compare bulk-copy and segmented output against the same flattened bytes
- verify aligned span decode only succeeds when legal
- verify unaligned payload decode uses safe access

## Acceptance Criteria

- Regression report identifies post-`v0.10.0` performance changes and likely
  source commits.
- Contiguous `as_bstr_range` approaches direct bstr encode performance.
- Direct binary string encode does not regress.
- Reserved definite bstr encode performs no additional allocations.
- Indefinite bstr output remains wire-correct and avoids avoidable chunk
  allocation/copy work.
- Raw array/map/item views can be decoded, re-encoded, and embedded in bstr
  without decoding child values.
- Segmented output can represent header-plus-borrowed-payload without copying.
- Typed-array bulk-copy rows stay near payload copy cost.
- Typed-array segment rows measure header-only encode cost with zero payload
  allocations.

## Assumptions

- Baseline means tag `v0.10.0`.
- Raw encoded views borrow from the original input buffer.
- Raw array/map views preserve original wire form, including indefinite form.
- Plain `as_bstr_range` remains a byte-string wrapper, not a typed-array API.
- True zero-copy encode is represented by segmented output, not by a single
  contiguous byte buffer.
