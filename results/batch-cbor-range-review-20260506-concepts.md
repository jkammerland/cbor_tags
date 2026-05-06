# CBOR Range Wrapper / Concept Classification Review

Scope: current uncommitted changes in:

- `include/cbor_tags/cbor.h`
- `include/cbor_tags/cbor_concepts.h`
- `include/cbor_tags/cbor_detail.h`
- `include/cbor_tags/cbor_encoder.h`
- `test/test_ranges.cpp`

Focus areas: explicit range wrapper encoding, concept/container classification, overload ambiguity, lifetime/API hazards, C++20 portability, Boost container compatibility, and regressions.

## Findings

### High: `as_map_range` accepts tuple-like values with more than two elements and silently drops data

References:

- `include/cbor_tags/cbor_concepts.h:266`
- `include/cbor_tags/cbor_concepts.h:274`
- `include/cbor_tags/cbor_encoder.h:421`
- `include/cbor_tags/cbor_encoder.h:422`
- `include/cbor_tags/cbor_encoder.h:423`
- `test/test_ranges.cpp:289`

`IsPairLike` only checks that `std::get<0>` and `std::get<1>` are valid, or that `.first` and `.second` exist. It does not require a tuple-like arity of exactly 2. As a result, `as_map_range` accepts ranges of `std::tuple<K, V, Extra...>` and `std::array<T, N>` for `N >= 2`. The encoder then writes only `pair_first(entry)` and `pair_second(entry)`, silently ignoring every remaining element.

This is a correctness bug because the explicit map wrapper presents these values as fully consumed map entries while discarding user data without a compile-time or runtime diagnostic. A direct probe with `std::array<std::tuple<int, int, int>, 1>{std::tuple{1, 2, 3}}` compiles and encodes as `a1 01 02`, dropping `3`.

Recommended fix: constrain tuple-like pair support to exactly two elements, e.g. require `std::tuple_size_v<std::remove_cvref_t<T>> == 2` for the `std::get` branch, while keeping `.first/.second` support for pair-like Boost/value proxy types. Add negative tests for triples and arrays of size other than 2.

### Medium: range wrapper encoding rejects valid move-only C++20 views

References:

- `include/cbor_tags/cbor.h:276`
- `include/cbor_tags/cbor.h:282`
- `include/cbor_tags/cbor.h:288`
- `include/cbor_tags/cbor_encoder.h:44`
- `include/cbor_tags/cbor_encoder.h:46`
- `include/cbor_tags/cbor_encoder.h:394`
- `include/cbor_tags/cbor_encoder.h:412`
- `include/cbor_tags/cbor_encoder.h:431`

The wrapper types store `R range_` where `R` is only constrained to `std::ranges::view`; C++20 views are required to be movable, not copyable. However `encoder::operator()` takes all user arguments as `const T&`, then calls `encode(args)`. The wrapper `encode` overloads take wrappers by value. That forces a copy from a const wrapper before the wrapper can be encoded.

This means move-only input views, including custom single-pass views and standard-style move-only view types, cannot be encoded through `as_array_range`, `as_map_range`, or `as_bstr_range`, even though the wrapper APIs are otherwise written in terms of `viewable_range`/`view`.

Verified failure mode: a minimal move-only `view_interface` over `int` satisfies the factory constraints, but `enc(as_array_range(std::move(view)))` fails in `encoder::operator()` because `array_range<move_only_view>` has a deleted copy constructor.

Recommended fix: make the top-level encoder call path preserve value category for wrapper arguments, or add wrapper-specific `operator()`/`encode` paths that move temporaries into the by-value overload. If changing `operator()` broadly is too risky, take wrapper overloads by forwarding reference or add overloads for `array_range<R>&&`, `map_range<R>&&`, and `bstr_range<R>&&` and route them before the const-reference path.

### Medium: `ValidCborBuffer` now accepts buffers that `make_encoder` cannot instantiate

References:

- `include/cbor_tags/cbor_concepts.h:40`
- `include/cbor_tags/cbor_concepts.h:41`
- `include/cbor_tags/cbor_concepts.h:42`
- `include/cbor_tags/cbor_concepts.h:43`
- `include/cbor_tags/cbor_encoder.h:29`
- `include/cbor_tags/cbor_encoder.h:35`
- `include/cbor_tags/cbor_encoder.h:36`
- `include/cbor_tags/cbor_encoder.h:224`
- `include/cbor_tags/cbor_detail.h:24`

`ValidCborBuffer` was generalized from nested container typedefs to range-based checks. That is useful for decoder input ranges, but the same concept still constrains `make_encoder`. The encoder implementation still requires `OutputBuffer::value_type`, `OutputBuffer::size_type`, and `detail::appender<OutputBuffer>`, whose `AppendableContainer` concept requires nested `value_type` and `size_type`.

The result is a constraint/API mismatch: `std::ranges::subrange` over a byte buffer now satisfies `ValidCborBuffer`, but `make_encoder(subrange)` fails during class instantiation because `subrange` has no `value_type`/`size_type` and is not appendable by the current appender.

Verified failure mode: `static_assert(ValidCborBuffer<decltype(std::ranges::subrange(vec.begin(), vec.end()))>)` passes, then `make_encoder(out)` fails at `cbor_encoder.h:35` and `cbor_encoder.h:224`.

Recommended fix: split input and output buffer concepts. Keep the range-based concept for decoder/tag scanning, and make `make_encoder` require a stricter output concept that matches the appender contract. Alternatively, update the encoder/appender to use range traits and support subrange output explicitly.

## Notes

- The Boost container paths compile and pass in `build-review-tests` on this machine after setting `CCACHE_DIR=/tmp/ccache`.
- `ctest --test-dir build-review-tests --output-on-failure` reports the doctest binary passing, but the `cbor_tags_cli` CTest entry fails because the CLI executable is not present in that build configuration.
- Direct range-focused doctest run: `./build-review-tests/test/tests --test-case="*range*" --reporter=console` passed.
- Direct Boost-focused doctest run: `./build-review-tests/test/tests --test-case="*boost*" --reporter=console` passed.
- No source files were edited.
