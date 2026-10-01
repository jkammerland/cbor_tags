# Expected

`<cbor_tags/expected.h>` provides `cbor::tags::expected<T, E>` in C++20. It is
independent of the encoder, decoder, fmt, and nameof. The implementation stores
the active value or error inline; payload types can still allocate internally.

There is no small-object size threshold or heap fallback. Both alternatives
share storage large enough for either payload, alongside a state flag and any
alignment padding. Large payloads therefore increase the size of the expected
object itself. `expected<void, E>` needs storage only for the error and state.

```cpp
#include <cbor_tags/expected.h>
#include <string>

namespace ct = cbor::tags;

ct::expected<int, std::string> positive(int number) {
    if (number <= 0)
        return ct::unexpected(std::string("must be positive"));
    return number;
}

auto doubled = positive(21).transform([](int n) { return n * 2; });
// doubled.value() == 42
auto recovered = positive(0).or_else([](const std::string&) {
    return ct::expected<int, std::string>(1);
});
```

## API and behavior

The API follows C++23 `std::expected`, including:

- Object and `void` values, move-only payloads, and in-place construction of
  immovable payloads.
- `unexpected<E>`, `unexpect`, `bad_expected_access<E>`, and their standard
  constructors and observers. `in_place` is also available in `cbor::tags`.
- Constrained converting constructors, assignments, `emplace`, comparisons,
  `swap`, `value_or`, and `error_or`.
- `and_then`, `or_else`, `transform`, and `transform_error`, with all four
  reference qualifiers. Callbacks run only for the relevant alternative.
- Constant evaluation and conditional trivial copy/move construction and
  destruction. Copy/move assignment uses the C++23 rules; C++26 trivial
  assignment and constexpr exceptions are outside this implementation's scope.

`and_then` must return an expected with the same error type. `or_else` must
preserve the value type. `transform` can return `void`, and both transforms can
construct an immovable result directly in its destination.

`value()` throws `bad_expected_access<E>` when an error is present. With compiler
exceptions disabled, that access calls `std::terminate`. Dereference, arrow, and
`error()` require the matching alternative; debug assertions check that
precondition. `value()` retains the standard's copy-constructible error
requirement, including its rvalue overloads.

State changes use a nonthrowing construction or move to keep an active
alternative if construction fails. The operations are constrained when neither
payload can support that guarantee. A payload's throwing assignment or move can
still modify that payload according to its own exception guarantee.

The implementation includes the bool-conversion correction in
[LWG 3836](https://cplusplus.github.io/LWG/issue3836), excludes `unexpect_t` from
value construction per [LWG 4222](https://cplusplus.github.io/LWG/issue4222), and
uses unqualified internal storage and transfers for cv-qualified values per
[LWG 3891](https://cplusplus.github.io/LWG/issue3891). Public observers and
assignment constraints retain the declared value type. GCC currently reports
some defaulted moves that copy const members as nontrivial. Those combinations
use the regular constructor path; the tests compare triviality against an
independent language probe and still check successful construction and lifetime
behavior.

## Backend selection and migration

The built-in implementation is the default in C++20 and newer. Existing
`cbor::tags::expected<T, E>` and `cbor::tags::unexpected<E>` uses keep their names.
`CBOR_TAGS_USE_STD_EXPECTED=ON` still selects `std::expected` with C++23, and
`CBOR_TAGS_STL_ONLY=ON` still selects the standard backend with C++26 reflection.
Use one backend consistently across translation units, including library
boundaries; these are distinct types with no promised binary compatibility.

The `tl-expected` dependency and `CBOR_TAGS_USE_SYSTEM_EXPECTED` option have been
removed. Code naming `tl::expected` directly must migrate to the public names or
manage its own dependency. Nonstandard tl extensions such as `map`, `map_error`,
and reference-valued expected are not provided; use `transform`,
`transform_error`, and a suitable owning value or `std::reference_wrapper`.

Installed CMake packages, Conan, and vcpkg no longer require an external expected
package. The public standalone header can also be included using just the
repository's `include` directory.

## Tests

The `expected/contract` suite is separate from codec roundtrips and CBOR wire
tests. It exercises state changes, payload lifetimes, throwing constructors,
move-only and immovable types, callback forwarding, constant evaluation, and
type constraints. CTest also checks invalid programs against specific compiler
diagnostics and checks normal execution and termination with exceptions disabled.

C++23 builds add comparisons with `std::expected`. Normative assertions cover
const-value assignment separately: libstdc++ 16 currently strips const in some
assignment constraints and is not a reliable oracle for that case.
