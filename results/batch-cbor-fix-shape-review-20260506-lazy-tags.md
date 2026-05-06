# Lazy Tag Scanner Shape Review

Reviewed commit: `cc72f8b` (`Add explicit range wrappers and lazy tag scanner`)

Scope: `include/cbor_tags/cbor_decoder.h`, `test/test_lazy_tags.cpp`, and `results/cbor-range-finding-proofs`.

## Findings

### 1. `find_tags` still accepts temporaries and can dangle

Severity: high

The chosen contract is lvalue-only: mutable and const lvalues are valid, temporaries must be a compile error. The current overloads are:

- `find_tags<Tags...>(const Buffer &buffer)` at `include/cbor_tags/cbor_decoder.h:2098`
- `find_tags(const Buffer &buffer, Predicate predicate)` at `include/cbor_tags/cbor_decoder.h:2102`

Both bind rvalues. `tag_view` then stores `const buffer_type *buffer_` (`include/cbor_tags/cbor_decoder.h:2091`), so a temporary vector/span/range object is destroyed while the returned view keeps a pointer to it.

Evidence: `results/cbor-range-finding-proofs/proof_find_tags_temporary_dangles.cpp` compiles `find_tags<100>(std::vector<std::byte>{...})`, and the proof harness triggers ASan. I ran `results/cbor-range-finding-proofs/run-proofs.sh`; the temporary-dangling proof passed.

Expected shape:

- Accept `Buffer &` so both mutable lvalues and const lvalues deduce correctly.
- Add explicit deleted rvalue overloads if clearer diagnostics are desired.
- Do not add an owning/range-copy fallback; that would violate the selected contract.

### 2. Scanner allocates during iteration and `std::bad_alloc` escapes

Severity: medium-high

The iterator owns `std::vector<detail::tag_scan_frame> stack_` (`include/cbor_tags/cbor_decoder.h:2074`). Normal scanning pushes frames for arrays, maps, and tags (`include/cbor_tags/cbor_decoder.h:2001`, `2008`, `2015`, `2026`, `2047`). Even a simple top-level matching tag pushes a tag frame before yielding.

There is no catch around `begin()`/`operator++()`, so allocation failure escapes as `std::bad_alloc` instead of becoming `status_code::out_of_memory`.

Evidence: `results/cbor-range-finding-proofs/proof_scanner_bad_alloc.cpp` overrides `operator new`, fails allocations before `view.begin()`, and observes an escaping `std::bad_alloc`. The proof harness reproduced this.

This is either a contract/documentation issue or a status propagation bug. Since `status_code::out_of_memory` exists and the decoder call wrapper maps `std::bad_alloc` to that status, scanner behavior should probably match.

### 3. Failure status is only meaningful after the lazy range is consumed to the point of failure

Severity: medium

`tag_view::status()` is shared view state and starts as `success` in `begin()` (`include/cbor_tags/cbor_decoder.h:2081`). It is updated when scanning reaches malformed or incomplete input (`fail()` at `include/cbor_tags/cbor_decoder.h:1859`).

That means a consumer can retrieve and decode the first match, stop early, and still see `success` even if later bytes are malformed. This may be acceptable for a lazy scanner, but tests should pin it down explicitly. Current tests cover initial failures (`test/test_lazy_tags.cpp:93` and `103`) and a fully consumed success path (`test/test_lazy_tags.cpp:73`), but not "match first, failure later".

Related risk: `begin()` resets `status_`, so re-beginning a failed view clears the previous failure before rescanning.

### 4. `payload_range()` and `payload_span()` are non-owning and depend on the original buffer lifetime

Severity: medium

`tag_match` stores only iterators into the source buffer (`include/cbor_tags/cbor_decoder.h:1804`) and returns a non-owning subrange/span (`include/cbor_tags/cbor_decoder.h:1808`, `1810`). This is consistent with "no copy/no owning view", but it makes the lvalue-only `find_tags` contract essential.

Risks to document/test:

- A copied `tag_match`, `payload_range()`, `payload_span()`, or `tag_payload_decoder` is valid only while the original buffer object and its storage remain alive and unmodified in ways that invalidate iterators.
- Temporary buffer handles such as temporary `std::span` objects must be rejected too, even if the underlying bytes outlive the span object.
- `payload_span()` is correctly constrained to contiguous buffers; non-contiguous buffers should keep using `payload_range()`.

### 5. `tag_payload_decoder` return types are inconsistent

Severity: medium

`tag_match::decode(T&)` builds a decoder and calls `dec(value)`, returning the configured `expected<void, status_code>` path (`include/cbor_tags/cbor_decoder.h:1819`).

`tag_payload_decoder::operator()` does the same (`include/cbor_tags/cbor_decoder.h:1786`), but `tag_payload_decoder::decode(T&)` calls `dec.decode(value)` and returns raw `status_code` (`include/cbor_tags/cbor_decoder.h:1791`).

Evidence: `results/cbor-range-finding-proofs/proof_payload_decoder_decode_type.cpp` has a `static_assert` proving `make_decoder().decode(value)` returns `status_code`.

The API should choose one public payload-decoder convention and test it. Given the surrounding user-facing call wrappers, the least surprising shape is likely for `match.decode(value)`, `match.make_decoder()(value)`, and `match.make_decoder().decode(value)` to agree.

### 6. The scanner structurally skips every tag payload before deciding whether to yield it

Severity: low-medium

When a tag is encountered, the scanner computes `payload_end` by calling `detail::cbor_item_skipper::skip_item(payload_end, end_, status)` before checking the predicate (`include/cbor_tags/cbor_decoder.h:2039`). For non-matching tags, it then continues scanning from the original `cursor_`, so that payload can be traversed again by the main scanner.

This is not a buffer copy, but it is not strictly single-pass for tagged payloads. It also means a malformed tail inside a non-matching tag can prevent earlier nested matching tags from being yielded, because the whole enclosing payload is validated before descending into it.

## Regression Test Suggestions

### Compile-time API shape

- Add compile-pass assertions that `find_tags<100>(buffer)` works for `std::vector<std::byte> &`.
- Add compile-pass assertions that `find_tags<100>(const_buffer)` works for `const std::vector<std::byte> &`.
- Add compile-pass assertions for the runtime predicate overload with mutable and const lvalues.
- Add compile-fail coverage for `find_tags<100>(std::vector<std::byte>{...})`.
- Add compile-fail coverage for `find_tags(std::vector<std::byte>{...}, predicate)`.
- Add compile-fail coverage for temporary `std::span<const std::byte>{buffer}` and temporary `std::ranges::subrange{...}`.
- Keep a compile-only non-copyable buffer wrapper test to prove the view does not copy or own the buffer.

### Allocation and status propagation

- Add an allocation-failure test like `proof_scanner_bad_alloc.cpp` to the maintained regression suite.
- Decide the expected behavior: no scanner allocations for ordinary nesting, or allocation failures converted to `status_code::out_of_memory`.
- Test allocation failure from both `begin()` and `++it`, since stack growth can happen after the first match.
- Add a test with a valid first match followed by malformed bytes; assert status before and after advancing to the failure point.
- Add a test with a valid first match followed by truncated bytes; assert `status_code::incomplete` after consuming to failure.
- Add tests for malformed indefinite maps/arrays after an earlier yielded match, not only at initial `begin()`.

### Payload lifetime and accessors

- Copy a `tag_match`, destroy the `tag_view` while keeping the source buffer alive, then verify `payload_range()`, `payload_span()`, and `make_decoder()` still work.
- Add an ASan or compile-fail guard proving temporary buffers/spans cannot produce payload ranges.
- Add a compile-time check that `payload_span()` is unavailable for `std::deque<std::byte>`.
- Add a contiguous-buffer test using `std::span<const std::byte>` as an lvalue to prove span inputs work without owning.
- Document and test that mutating/reallocating the source buffer after taking a payload range is outside the valid lifetime contract.

### Payload decoder return type

- Add type assertions for:
  - `decltype(match.decode(value))`
  - `decltype(match.make_decoder()(value))`
  - `decltype(match.make_decoder().decode(value))`
- Make those assertions enforce the chosen single convention.
- Add one decode-failure test through each public payload decoder entry point and assert the same error representation is returned.

## Verification Run

Command run:

```bash
results/cbor-range-finding-proofs/run-proofs.sh
```

Result: passed. The proof suite reproduced the temporary dangling issue, scanner allocation/escaping `std::bad_alloc`, and raw `status_code` payload-decoder `decode()` return type.
