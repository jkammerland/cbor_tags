# CBOR range/lazy-tag test coverage review - 2026-05-06

Scope reviewed:
- `test/test_ranges.cpp`
- `test/test_decoder_regressions.cpp`
- `test/test_lazy_tags.cpp`
- `include/cbor_tags/cbor_decoder.h`
- `include/cbor_tags/cbor_encoder.h`
- related uncommitted concept/detail changes used by those paths

Verification notes:
- The existing `build/` cache has `CBOR_TAGS_BUILD_TESTS=OFF`, so `ctest --test-dir build` was running stale test binaries from May 3 and did not include the new cases.
- A fresh temporary build with tests enabled was configured at `/tmp/cbor-tags-range-review-build`.
- `CCACHE_DIR=/tmp/ccache cmake --build /tmp/cbor-tags-range-review-build --parallel` passed.
- `CCACHE_DIR=/tmp/ccache ctest --test-dir /tmp/cbor-tags-range-review-build --output-on-failure` passed.
- The fresh test binary listed and ran the new lazy scanner, range wrapper, Boost, and duplicate-map cases.

## Findings

### High: `find_tags` can dangle on temporaries, and tests only use named lvalue buffers

`find_tags` accepts `const Buffer &` and `tag_view` stores a raw pointer to that buffer. A call such as:

```cpp
auto view = find_tags<100>(to_bytes("d864182a"));
```

constructs a view whose `buffer_` points at a destroyed temporary before iteration starts. The current tests always materialize a local buffer first, e.g. `auto buffer = to_bytes(...)` followed by `find_tags(buffer)`, so this lifetime hole is not covered.

References:
- `test/test_lazy_tags.cpp:16`
- `test/test_lazy_tags.cpp:18`
- `include/cbor_tags/cbor_decoder.h:2079`
- `include/cbor_tags/cbor_decoder.h:2091`
- `include/cbor_tags/cbor_decoder.h:2098`
- `include/cbor_tags/cbor_decoder.h:2102`

Suggested coverage:
- Add a compile-time/API guard test that `find_tags<...>(temporary_buffer)` is rejected, or change the API to own rvalue buffers and add a runtime test for that supported behavior.
- Add the same check for the predicate overload.

### High: Range wrapper tests miss rvalue/owning views, which currently fail to compile

The new wrappers are built around `std::views::all`, which can produce move-only `std::ranges::owning_view` for rvalue containers. The encoder call path takes arguments as `const T &` and the wrapper encode overloads take `array_range<R>`, `map_range<R>`, and `bstr_range<R>` by value. That requires copying the wrapper out of a const reference, which fails for move-only views.

Reproduced with:

```cpp
std::vector<std::byte> out;
auto enc = make_encoder(out);
auto r = enc(as_array_range(std::vector<int>{1, 2, 3}));
```

The existing tests only pass named, copyable lvalue views (`sized`, `evens`, `sized_pairs`, `odd_pairs`, `sized_bytes`, `chunked_bytes`), so they do not exercise the advertised `viewable_range` surface.

References:
- `test/test_ranges.cpp:279`
- `test/test_ranges.cpp:284`
- `test/test_ranges.cpp:293`
- `test/test_ranges.cpp:298`
- `test/test_ranges.cpp:308`
- `test/test_ranges.cpp:313`
- `include/cbor_tags/cbor_encoder.h:44`
- `include/cbor_tags/cbor_encoder.h:46`
- `include/cbor_tags/cbor_encoder.h:394`
- `include/cbor_tags/cbor_encoder.h:412`
- `include/cbor_tags/cbor_encoder.h:431`

Suggested coverage:
- Add compile tests for `as_array_range(std::vector<int>{...})`, `as_map_range(std::vector<std::pair<int, int>>{...})`, and `as_bstr_range(std::vector<std::byte>{...})`.
- Include at least one move-only custom view if rvalue containers are not intended to be the only move-only case.

### Medium: Lazy scanner stack behavior is only lightly covered on successful indefinite and nested-tag inputs

The scanner has non-trivial state for definite frames, indefinite frames, map key/value alternation, tag payload frames, and payload skipping. The tests cover successful definite arrays/maps and malformed indefinite items, but they do not cover successful indefinite arrays/maps containing matching tags, chained tags, matching tags nested inside an unmatched tag payload, or valid indefinite byte/text strings near tags.

That leaves the most fragile code paths in `consume_parent_item`, `consume_indefinite_break_if_present`, and the `Array`/`Map`/`Tag` cases under-tested.

References:
- `test/test_lazy_tags.cpp:15`
- `test/test_lazy_tags.cpp:103`
- `include/cbor_tags/cbor_decoder.h:1870`
- `include/cbor_tags/cbor_decoder.h:1888`
- `include/cbor_tags/cbor_decoder.h:1999`
- `include/cbor_tags/cbor_decoder.h:2013`
- `include/cbor_tags/cbor_decoder.h:2031`

Suggested coverage:
- Indefinite array with a matching tag before the break.
- Indefinite map with matching tags in both key and value positions.
- Chained tags such as `tag(100, tag(200, 1))`, asserting both outer and inner payload ranges.
- Unmatched outer tag with a matching nested tag in its payload.
- Valid chunked byte/text strings that contain no tags and are followed by a tag.

### Medium: Duplicate-map tests lock in last-wins for `std::map` without making the duplicate-key policy explicit

The duplicate-key test says "target container insertion semantics" but asserts `std::map` keeps the later value. That follows the current `insert_or_assign` path, not ordinary `std::map::insert` semantics, and it does not clarify whether generic CBOR maps should accept duplicates, reject them, or use container-specific overwrite behavior.

The `std::multimap` half only checks `count(1) == 2`, so it would not catch swapped, duplicated, or incorrectly decoded mapped values.

References:
- `test/test_decoder_regressions.cpp:699`
- `test/test_decoder_regressions.cpp:707`
- `test/test_decoder_regressions.cpp:708`
- `test/test_decoder_regressions.cpp:716`
- `include/cbor_tags/cbor_decoder.h:303`
- `include/cbor_tags/cbor_decoder.h:314`
- `include/cbor_tags/cbor_decoder.h:1088`
- `include/cbor_tags/cbor_decoder.h:1095`

Suggested coverage:
- Rename or split the test to state the intended duplicate-key policy directly.
- Add `std::unordered_map` and pre-populated target containers.
- For `std::multimap`, assert the actual `equal_range` values are `{1, 2}`, not just the count.
- Add an indefinite-map duplicate case so both definite and indefinite paths are locked.

### Medium: Boost coverage is mostly classification and happy-path map decode

The Boost tests statically assert several concepts and encode/decode a small subset, but the riskiest container-specific behavior is not exercised:

- `boost::container::vector`, `list`, `small_vector`, and `static_vector` are only concept-checked as arrays.
- `boost::container::string` is only concept-checked as text.
- `boost::container::static_vector` capacity overflow is not tested.
- Boost unique maps are not tested with duplicate keys.
- Boost multi-map containers are not covered.
- Boost map decoding is only definite and non-duplicate.

References:
- `test/test_ranges.cpp:320`
- `test/test_ranges.cpp:323`
- `test/test_ranges.cpp:327`
- `test/test_ranges.cpp:332`
- `test/test_ranges.cpp:337`
- `test/test_ranges.cpp:344`
- `test/test_ranges.cpp:349`
- `test/test_ranges.cpp:358`
- `test/test_ranges.cpp:367`

Suggested coverage:
- Roundtrip a Boost vector/list/small_vector/static_vector array.
- Decode too many elements into a `boost::container::static_vector`.
- Roundtrip a `boost::container::string`.
- Add duplicate-key decode cases for Boost unique maps and Boost multi-maps.
- Add an indefinite Boost map decode case.

### Low: Range wrapper edge cases are currently byte-golden only

The wrapper tests compare exact hex output but do not decode the generated bytes back into normal containers. They also miss empty ranges, one-element chunking, `chunk_size == 0`, signed/char byte-like inputs, tuple-like map entries, and output-buffer failure behavior.

References:
- `test/test_ranges.cpp:275`
- `test/test_ranges.cpp:289`
- `test/test_ranges.cpp:304`
- `include/cbor_tags/cbor_encoder.h:396`
- `include/cbor_tags/cbor_encoder.h:414`
- `include/cbor_tags/cbor_encoder.h:433`
- `include/cbor_tags/cbor_encoder.h:440`
- `include/cbor_tags/cbor_encoder.h:458`

Suggested coverage:
- Decode the produced wrapper bytes and assert semantic roundtrip.
- Add empty sized and non-sized array/map/bstr wrapper cases.
- Add `as_bstr_range(non_sized, 0)` and assert `status_code::error` with no partial output.
- Add `std::tuple<int, int>` entries for `as_map_range`.
- Add `char`, `signed char`, `unsigned char`, and `std::uint8_t` byte-like bstr cases.

