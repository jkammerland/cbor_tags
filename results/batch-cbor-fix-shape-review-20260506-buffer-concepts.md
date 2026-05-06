# CBOR Buffer Concept Split Review

Scope: review after `cc72f8b` focused on splitting decoder input buffers from encoder output buffers. No library or test files were modified.

Checks run:

- `results/cbor-range-finding-proofs/run-proofs.sh`
- Temporary compile/run probe proving `std::ranges::subrange` is currently `ValidCborBuffer`, is also `std::ranges::output_range<..., std::byte>`, and remains decodable via `make_decoder`.

## Summary

The split is the right shape. The current `ValidCborBuffer` in `include/cbor_tags/cbor_concepts.h:40` is an input-buffer concept: it validates const-readable byte ranges, including `std::ranges::subrange`. Reusing it for `encoder` and `make_encoder` is the root mismatch. A payload `subrange` must stay valid for decoder input, but it should be rejected at the encoder API boundary before `encoder` instantiates.

## Findings

### 1. `ValidCborBuffer` is input-shaped but gates encoder output

`ValidCborBuffer` now requires range/common/byte/contiguous-or-bidirectional properties on `const remove_cvref_t<T>` (`cbor_concepts.h:40-43`). That is exactly what decoder and tag payload slices need, but it does not describe an output buffer.

The encoder still uses it at `cbor_encoder.h:29-31`. A mutable `std::ranges::subrange` passes the concept, then `encoder` fails inside the class body because it assumes `OutputBuffer::value_type`, `OutputBuffer::size_type`, and `detail::appender<OutputBuffer>` (`cbor_encoder.h:35-36`, `cbor_encoder.h:224`).

Evidence: `results/cbor-range-finding-proofs/compile_fail_subrange_encoder.cpp` proves `ValidCborBuffer<subrange>` is true while `make_encoder(subrange)` hard-fails during instantiation.

Recommendation: introduce separate public concepts, for example:

- `CborDecoderInputBuffer`: current const-readable byte range semantics.
- `CborEncoderOutputBuffer`: mutable byte sink semantics matching the encoder appender paths.

Then constrain both `encoder` and `make_encoder` with `CborEncoderOutputBuffer`, and constrain `decoder`, `make_decoder`, `tag_view`, `tag_match`, `tag_payload_decoder`, `find_tags`, and `detail::reader` with `CborDecoderInputBuffer`.

### 2. Do not define encoder output as only `std::ranges::output_range`

A mutable `std::ranges::subrange` of `std::vector<std::byte>::iterator` satisfies `std::ranges::output_range<subrange, std::byte>`. Using that as the encoder-output concept would preserve the exact accidental acceptance this split is meant to remove.

The output concept should be positive and API-shaped. It should accept the actual supported sinks:

- appendable byte containers such as `std::vector<std::byte>`, `std::vector<std::uint8_t>`, PMR vectors, and similar containers with `value_type`, `size_type`, `push_back`, `insert(end, ...)`;
- fixed mutable byte storage such as `std::array<std::byte, N>`, `std::array<std::uint8_t, N>`, and `std::span<std::byte>`.

It should reject `std::ranges::subrange`, `std::string_view`, `std::span<const std::byte>`, const containers, and non-byte ranges. Avoid a blanket `!std::ranges::view` rule because `std::span<std::byte>` is a view but is a legitimate fixed output buffer.

### 3. `detail::appender` is shared by encoder output and decoded target containers

`detail::appender` in `cbor_detail.h:24-105` is used by the encoder, but also by decoder container/string/map paths (`cbor_decoder.h:303`, `cbor_decoder.h:970`, `cbor_decoder.h:1006`, `cbor_decoder.h:1042`, `cbor_decoder.h:1074`). Tightening `detail::AppendableContainer` to mean "encoder output buffer" would cause compile fallout in ordinary decode targets such as `std::vector<int>`, `std::map<int, int>`, `std::multimap`, and strings.

Recommendation: keep the generic decoded-container appender broad, or split it into a generic `container_appender` and an encoder-specific `output_appender`. Put the new output-buffer concept at the encoder API boundary, not on all appender users.

### 4. Tag payload decoding depends on subrange staying decoder-valid

The lazy tag scanner returns payload slices as `std::ranges::subrange` (`cbor_decoder.h:1808`). Both `tag_payload_decoder::operator()` and `tag_match::decode` immediately call `make_decoder(range_)` or `make_decoder(range)` (`cbor_decoder.h:1786-1793`, `cbor_decoder.h:1819-1822`).

If the existing `ValidCborBuffer` name is repurposed to output-only, this code will fail. If it remains as an ambiguous shared concept, the encoder mismatch remains.

Recommendation: make these tag payload types explicitly use `CborDecoderInputBuffer`. Add regression coverage that `payload_range()` can be passed to `make_decoder`, while the same range cannot be passed to `make_encoder`.

### 5. `CborStream` and `detail::iterator_type` need explicit ownership after the split

`CborStream` is constrained with `ValidCborBuffer` but assumes `Buffer::size_type` (`cbor_concepts.h:616-622`). If `ValidCborBuffer` remains input-shaped, `std::ranges::subrange` can pass the concept and still fail on `Buffer::size_type`. If `CborStream` is output-oriented, it should use `CborEncoderOutputBuffer`.

`detail::iterator_type` now uses `std::ranges::iterator_t<const remove_cvref_t<T>>` (`cbor_concepts.h:672-674`), which is input-oriented. That is appropriate for decoder subranges and `cbor.h` range variants, but the encoder's `using iterator_type/subrange` aliases (`cbor_encoder.h:37-38`) should either be removed if unused or switched to a mutable output iterator helper. Leaving one helper to mean both const input and mutable output invites another accidental coupling.

### 6. Factory functions should be constrained too

`make_encoder` is currently unconstrained at the function signature (`cbor_encoder.h:481-484`), so invalid output shapes produce noisy class-instantiation diagnostics. The split should constrain the factory itself:

- `template <CborEncoderOutputBuffer OutputBuffer> auto make_encoder(OutputBuffer &buffer)`
- `template <CborDecoderInputBuffer InputBuffer> auto make_decoder(const InputBuffer &buffer)` or an equivalent lvalue-reference form preserving current API behavior.

This gives the subrange failure at overload resolution instead of inside `encoder`.

## Risks

- Public API naming risk: downstream code may already use `ValidCborBuffer`. Keeping it as a deprecated alias to `CborDecoderInputBuffer` is safer than silently changing it to output semantics.
- Over-constraining output buffers could drop existing supported sinks, especially `std::array`, `std::span<std::byte>`, PMR vectors, and one-byte integral containers.
- Under-constraining with `std::ranges::output_range` would not fix the bug because mutable subranges satisfy it.
- Tightening `detail::appender` globally would break decoder compile paths for decoded arrays, maps, strings, and non-default allocator/comparator containers.
- Broader extension code uses `ValidCborBuffer` for diagnostic input (`include/cbor_tags/extensions/cbor_visualization.h:1772`). If concept names change, input-facing extension APIs need the decoder-input concept.

## Regression Test Suggestions

Add compile-time concept tests near the buffer concept definitions:

- `CborDecoderInputBuffer<std::vector<std::byte>>`
- `CborDecoderInputBuffer<std::span<const std::byte>>`
- `CborDecoderInputBuffer<std::ranges::subrange<std::vector<std::byte>::const_iterator>>`
- `CborEncoderOutputBuffer<std::vector<std::byte>>`
- `CborEncoderOutputBuffer<std::vector<std::uint8_t>>`
- `CborEncoderOutputBuffer<std::array<std::byte, 8>>`
- `CborEncoderOutputBuffer<std::span<std::byte>>`
- `!CborEncoderOutputBuffer<std::span<const std::byte>>`
- `!CborEncoderOutputBuffer<std::string_view>`
- `!CborEncoderOutputBuffer<std::ranges::subrange<std::vector<std::byte>::iterator>>`
- `std::ranges::output_range<subrange, std::byte>` remains true while `CborEncoderOutputBuffer<subrange>` remains false, to prevent replacing the output concept with `output_range`.

Add runtime decoder coverage:

- Encode or hand-build `d864182a`, scan with `find_tags<100>`, call `payload_range()`, then `make_decoder(payload_range)` and decode `42`.
- Exercise `it->make_decoder()(value)` and `it->decode(value)` for the same payload.
- Repeat with a non-contiguous source buffer such as `std::deque<std::byte>` to keep bidirectional input support covered.

Add compile-fail coverage in a small CMake `try_compile` or standalone negative-test script:

- `make_encoder(std::ranges::subrange(storage.begin(), storage.end()))` must not compile because the output concept is not satisfied.
- `make_decoder(std::ranges::subrange(storage.begin(), storage.end()))` must compile.
- `make_encoder(std::span<const std::byte>)` and `make_encoder(std::string_view)` must not compile.

Keep positive encoder-output coverage:

- `make_encoder(std::vector<std::byte>&)`
- `make_encoder(std::vector<std::uint8_t>&)`
- `make_encoder(std::array<std::byte, N>&)` with enough and insufficient capacity
- `make_encoder(std::span<std::byte>)`

Re-run existing decoder container regressions after the split, especially `test_decoder_regressions.cpp` cases for duplicate map keys, non-default comparators, non-assignable comparators, and non-default allocators. Those protect against accidental `detail::appender` over-tightening.
