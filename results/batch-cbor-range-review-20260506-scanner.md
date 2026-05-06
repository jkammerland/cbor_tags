# Lazy Tag Scanner Review - 2026-05-06

Scope:
- `include/cbor_tags/cbor_decoder.h`
- `test/test_lazy_tags.cpp`

Verification:
- Configured a separate `/tmp/cbor-tags-review-build` with `CBOR_TAGS_BUILD_TESTS=ON`.
- Initial build attempt failed because ccache tried to write under read-only `/home/ai-dev1/.cache/ccache`.
- Rebuilt with `CCACHE_DISABLE=1`; current code compiled and `test/tests --test-case="*lazy tag scanner*"` passed: 7 test cases, 32 assertions.

## Findings

### P1 - `find_tags` can return a dangling view for temporary buffers

References:
- `include/cbor_tags/cbor_decoder.h:2079`
- `include/cbor_tags/cbor_decoder.h:2091`
- `include/cbor_tags/cbor_decoder.h:2098`
- `include/cbor_tags/cbor_decoder.h:2102`
- `test/test_lazy_tags.cpp:16`
- `test/test_lazy_tags.cpp:50`

`tag_view` stores `const buffer_type *buffer_`, and both `find_tags` overloads accept `const Buffer &` then return a view containing that pointer. Calls like this compile:

```cpp
auto view = cbor::tags::find_tags<100>(to_bytes("d864182a"));
auto it = view.begin();
```

The temporary returned by `to_bytes` is destroyed at the end of the `find_tags` full expression, leaving `tag_view::buffer_` dangling before `begin()` dereferences it. The same hazard applies to temporary `std::span`/`std::ranges::subrange` objects because the view stores a pointer to the range object, not copied iterators or an owning/view wrapper.

The current tests only pass lvalue buffers, so they do not exercise the public helper's rvalue behavior.

Recommended direction:
- Reject rvalues explicitly, e.g. delete `const Buffer &&` overloads, if scanner views are intended to be non-owning.
- Or store a proper view object such as a `views::all_t`/owning wrapper rather than a raw pointer to the function parameter.
- Add a compile-time/API test that makes the intended lifetime contract unambiguous.

### P1 - Scanner traversal allocates and can throw instead of reporting `out_of_memory`

References:
- `include/cbor_tags/cbor_decoder.h:99`
- `include/cbor_tags/cbor_decoder.h:109`
- `include/cbor_tags/cbor_decoder.h:2001`
- `include/cbor_tags/cbor_decoder.h:2008`
- `include/cbor_tags/cbor_decoder.h:2015`
- `include/cbor_tags/cbor_decoder.h:2026`
- `include/cbor_tags/cbor_decoder.h:2047`
- `include/cbor_tags/cbor_decoder.h:2074`

The lazy scanner iterator owns `std::vector<detail::tag_scan_frame> stack_` and pushes frames for arrays, maps, and every tag payload. Even a top-level tag requires `stack_.push_back(...)` before the match is yielded. That means `begin()`/`operator++()` can allocate during traversal.

This conflicts with the no-allocation expectation for a lazy scanner and also bypasses the library's usual exception-to-status behavior: `decoder::operator()` catches `std::bad_alloc` and returns `status_code::out_of_memory`, but `tag_view::iterator::find_next()` does not catch allocation failures from the traversal stack. A failed allocation escapes as an exception instead of updating `view.status()`.

The current tests do not install an allocation guard or verify that scanning a simple tagged value is allocation-free.

Recommended direction:
- Replace the vector-backed traversal stack with a fixed-capacity/no-allocation strategy if allocation-free scanning is a requirement.
- If dynamic allocation is accepted, document it and catch `std::bad_alloc` in scanner entry points so `status_code::out_of_memory` remains consistent.
- Add an allocation-failure/no-allocation regression test for at least a top-level tag and nested array/map/tag traversal.

### P2 - Tag payload endpoint discovery recursively rescans nested payloads

References:
- `include/cbor_tags/cbor_decoder.h:1663`
- `include/cbor_tags/cbor_decoder.h:1697`
- `include/cbor_tags/cbor_decoder.h:1707`
- `include/cbor_tags/cbor_decoder.h:1724`
- `include/cbor_tags/cbor_decoder.h:1735`
- `include/cbor_tags/cbor_decoder.h:1749`
- `include/cbor_tags/cbor_decoder.h:1757`
- `include/cbor_tags/cbor_decoder.h:2039`
- `include/cbor_tags/cbor_decoder.h:2042`

For each tag encountered, `find_next()` computes `payload_end` by calling `detail::cbor_item_skipper::skip_item(payload_end, end_, status)`. That skipper recursively descends into arrays, maps, tags, and indefinite containers.

This creates two problems for nested CBOR:
- Deeply nested tag/container payloads can overflow the C++ call stack before the scanner yields a match.
- Chains of nested tags are rescanned repeatedly: the outer tag skip walks the whole nested payload, then the scanner descends into it and the next tag skip walks the remainder again. A depth-N tag chain becomes quadratic traversal work.

This is especially relevant because the iterator traversal itself is otherwise stack-frame based; the recursive payload-end side path reintroduces depth-sensitive behavior. The tests cover only a shallow nested definite array/map case.

Recommended direction:
- Compute payload endpoints with the same iterative traversal model as the scanner, ideally sharing one well-formedness walker.
- Add deep-but-small nested tag/container tests that would catch recursion limits and repeated-scan regressions.

### P3 - `tag_payload_decoder::decode` has inconsistent exception/status behavior

References:
- `include/cbor_tags/cbor_decoder.h:1786`
- `include/cbor_tags/cbor_decoder.h:1791`
- `include/cbor_tags/cbor_decoder.h:1817`
- `include/cbor_tags/cbor_decoder.h:1819`
- `include/cbor_tags/cbor_decoder.h:1821`

`tag_match::decode(T&)` routes through `decoder::operator()`, so callers get the expected-return API with exception translation. `tag_payload_decoder::operator()` does the same. In contrast, `tag_payload_decoder::decode(T&)` calls the low-level `dec.decode(value)` directly.

That makes `auto dec = match.make_decoder(); dec.decode(value);` behave differently from both `match.decode(value)` and `dec(value)`: it returns a raw `status_code` path and can let allocation or parsing exceptions escape from target decoding instead of using the expected/status wrapper.

Recommended direction:
- Either remove/rename the low-level helper or implement it in terms of `operator()(value)` so all payload decoder entry points have the same error behavior.
- Add a test that exercises `make_decoder().decode(...)`, not only `make_decoder()(...)`.

### P3 - Tests leave key scanner traversal classes uncovered

References:
- `test/test_lazy_tags.cpp:15`
- `test/test_lazy_tags.cpp:31`
- `test/test_lazy_tags.cpp:49`
- `test/test_lazy_tags.cpp:58`
- `test/test_lazy_tags.cpp:73`
- `test/test_lazy_tags.cpp:93`
- `test/test_lazy_tags.cpp:103`

The new tests cover shallow definite nesting, runtime predicates, contiguous spans, non-contiguous buffers, one large definite byte string, one truncated matching payload, and two malformed indefinite cases.

They do not cover:
- Successful indefinite arrays/maps containing tags.
- Successful indefinite byte/text strings, including tags adjacent to but outside string chunks.
- Nested tags inside tag payloads, especially tag-over-tag chains.
- Truncated definite arrays/maps/strings and truncated tag arguments.
- Invalid additional-info forms for tags/simple values/containers.
- Iterator lifetime after copying a `tag_match` and incrementing the scanner.
- Temporary-buffer rejection/ownership behavior.
- Allocation-free or allocation-failure behavior.

These gaps matter because most of the scanner's correctness risk is in exactly those traversal and lifetime cases.
