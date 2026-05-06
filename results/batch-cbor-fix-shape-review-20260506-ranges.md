# Range Adapter Fix Shape Review

Scope: commit `cc72f8b`, focused on `as_map_range` pair-like detection, rvalue/owning range wrapper encoding, and `bstr_range` chunking semantics. No library or test files were modified.

## Findings

### High: `as_map_range` must reject tuple-like entries whose `tuple_size` is not exactly 2

References:
- `include/cbor_tags/cbor_concepts.h:266`
- `include/cbor_tags/cbor_concepts.h:275`
- `include/cbor_tags/cbor_detail.h:230`
- `include/cbor_tags/cbor_detail.h:238`
- `include/cbor_tags/cbor_encoder.h:421`

Current `IsPairLike` accepts any type where `std::get<0>` and `std::get<1>` are valid. It does not check tuple arity, while `map_range` encoding only writes `pair_first(entry)` and `pair_second(entry)`. A tuple-like entry with extra fields therefore compiles and silently drops data.

Verified proof: `std::array<std::tuple<int, int, int>, 1>{std::tuple{1, 2, 3}}` passed `as_map_range` and encoded as `a10102`, dropping the third tuple field.

The proposed direction is correct: split tuple-like and member-pair detection. The tuple-like branch should require `std::tuple_size_v<std::remove_cvref_t<T>> == 2`; the `.first/.second` branch should remain independently accepted for member-pair entries and map proxy types.

Risk: this is a deliberate source break for callers passing `tuple`, `array`, or custom tuple-like entries with arity other than 2. That break is preferable to silent truncation, but tests should lock down accepted `std::tuple<K,V>` and member-pair entries so the fix does not over-constrain map-like proxies.

### High: encoder forwarding still blocks rvalue/owning wrappers

References:
- `include/cbor_tags/cbor.h:276`
- `include/cbor_tags/cbor.h:282`
- `include/cbor_tags/cbor.h:288`
- `include/cbor_tags/cbor.h:295`
- `include/cbor_tags/cbor_encoder.h:44`
- `include/cbor_tags/cbor_encoder.h:46`
- `include/cbor_tags/cbor_encoder.h:394`
- `include/cbor_tags/cbor_encoder.h:412`
- `include/cbor_tags/cbor_encoder.h:431`

The factories correctly normalize inputs through `std::views::all_t<R>`, so rvalue containers become owning views. However, `encoder::operator()` takes `const T&...` and calls `encode(args)`. The wrapper encode overloads then take `array_range<R>`, `map_range<R>`, and `bstr_range<R>` by value. That path requires copying a wrapper out of a const reference before encoding.

Verified proof: `enc(as_array_range(std::vector<int>{1, 2, 3}))` fails to compile because `array_range<std::ranges::owning_view<std::vector<int>>>` has a deleted copy constructor.

The proposed direction is correct: `encoder::operator()` should take forwarding references and call `encode(std::forward<T>(args))`. The wrapper encode overloads also need to avoid copy requirements for move-only views. The most robust shape is wrapper overloads that bind non-copying references for lvalue wrappers and rvalue wrappers, or an equivalent forwarding overload. Relying only on by-value wrapper overloads fixes temporary owning wrappers after top-level forwarding, but still rejects named lvalue wrappers around move-only views unless callers explicitly `std::move` them.

Risk: broad forwarding can affect custom `encode`/ADL overload resolution for user types. Add a small compatibility check for existing const lvalue class encoding and free `encode` dispatch when changing the top-level operator.

### Medium: `bstr_range` chunking semantics need to be made explicit

References:
- `include/cbor_tags/cbor.h:288`
- `include/cbor_tags/cbor.h:307`
- `include/cbor_tags/cbor_encoder.h:431`
- `include/cbor_tags/cbor_encoder.h:433`
- `include/cbor_tags/cbor_encoder.h:439`
- `include/cbor_tags/cbor_encoder.h:440`
- `include/cbor_tags/cbor_encoder.h:458`
- `test/test_ranges.cpp:308`
- `test/test_ranges.cpp:313`

`as_bstr_range(range, chunk_size)` stores a chunk size for every range, but encoding uses it only when `R` is not a `sized_range`. Sized byte ranges always emit a definite byte string and ignore `chunk_size`; non-sized ranges emit an indefinite byte string split into chunks. `chunk_size == 0` is rejected only on the non-sized path.

This may be the right design if chunking is only a streaming fallback. It is still an observable API contract, and the forwarding fix will make more rvalue/owning inputs reach this code path. In particular, `as_bstr_range(std::vector<std::byte>{...}, 2)` will encode as one definite byte string because `std::ranges::owning_view<std::vector<std::byte>>` is sized.

Risk: callers may read `chunk_size` as "force chunked encoding". If the intended contract is "sized ranges are definite; chunking applies only to non-sized ranges", tests and docs should say so. If chunk size is meant to force chunking, the sized branch needs different behavior.

## Regression Test Suggestions

### Pair-like concept and map wrapper

- Add compile-time checks that `std::tuple<int, int>`, `std::pair<int, int>`, and a custom `{ first, second }` member-pair type satisfy the pair-like concept.
- Add compile-time negative checks for `std::tuple<int, int, int>`, `std::tuple<int>`, and `std::array<int, 3>`.
- Decide and test whether `std::array<int, 2>` is an accepted tuple-like map entry; the proposed `tuple_size == 2` rule would accept it.
- Add a runtime test that `as_map_range(std::array{std::tuple{1, 2}})` encodes as `a10102`.
- Add a compile-fail/API guard that `as_map_range(std::array{std::tuple{1, 2, 3}})` is rejected.
- Add a runtime test for a member-pair entry type with `.first` and `.second` but no tuple interface, to ensure that branch remains accepted.

### Forwarding and owning views

- Add direct rvalue container tests:
  - `enc(as_array_range(std::vector<int>{1, 2, 3}))` -> `83010203`
  - `enc(as_map_range(std::vector<std::pair<int, int>>{{1, 2}, {3, 4}}))` -> `a201020304`
  - `enc(as_bstr_range(std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}}))` -> `43010203`
- Add a move-only custom `std::ranges::view_interface` fixture and verify at least the intended supported form:
  - `enc(as_array_range(std::move(view)))` if named move-only views are move-only-call-only.
  - `auto wrapped = as_array_range(std::move(view)); enc(wrapped)` if wrapper encode overloads are intended to support lvalue wrappers without copying.
- Add a forwarding regression for existing custom encoding dispatch: const lvalue object with member `encode`, and const lvalue object with ADL `encode`.

### `bstr_range` chunking

- Keep the existing non-sized chunk test and add direct rvalue coverage for the same shape.
- Add a sized range with explicit `chunk_size`, e.g. `as_bstr_range(std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}}, 2)`, and assert the intended contract:
  - definite `43010203` if sized ranges ignore chunking, or
  - chunked `5f4201024103ff` if `chunk_size` is meant to force chunking.
- Add `as_bstr_range(non_sized_bytes, 1)` to lock down one-byte chunk output.
- Add `as_bstr_range(empty_sized_bytes)` -> `40` and `as_bstr_range(empty_non_sized_bytes, 2)` -> `5fff` if the current sized/non-sized distinction is intended.
- Add `as_bstr_range(non_sized_bytes, 0)` and assert `status_code::error` with no partial output. Also decide and test whether `as_bstr_range(sized_bytes, 0)` should be accepted as definite output or rejected consistently.

## Evidence Checked

- Compiled and ran `results/cbor-range-finding-proofs/proof_map_range_triple.cpp`; it reproduced tuple-like truncation as `a10102`.
- Compiled `results/cbor-range-finding-proofs/compile_fail_rvalue_range.cpp`; it failed as expected on a deleted copy of `array_range<std::ranges::owning_view<std::vector<int>>>`.
