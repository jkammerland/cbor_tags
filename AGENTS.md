# Repository Guidelines

## Project Structure & Module Organization
- `include/cbor_tags/` holds the header-only encoder/decoder stack, tag traits, and reflection helpers generated under `include/cbor_tags/detail`.
- `test/` bundles doctest-based suites (`test_*.cpp`) that compile into a single `tests` binary; shared fixtures live in `test_util/`.
- `examples/` offers focused usage samples you can adapt for docs or regression cases.
- `tools/` contains maintenance utilities such as `reflection_module_generator.cpp` for regenerating reflection tables.
- `benchmarks/` and `doc/` host performance probes and longer-form documentation; keep them aligned with API changes.

## Build, Test, and Development Commands
```bash
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCBOR_TAGS_BUILD_TESTS=ON \
  -DCBOR_TAGS_USE_DEV_SETTINGS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/test/tests --reporter=console --success
```
- Switch to Release builds by setting `-DCMAKE_BUILD_TYPE=Release` and toggling benchmark/tool flags (`DCBOR_TAGS_BUILD_BENCHMARKS`, `DCBOR_TAGS_BUILD_TOOLS`).
- Enable clang-tidy with `-DCBOR_TAGS_TIDY_TARGET=ON` or sanitizers via `DCBOR_TAGS_ENABLE_ASAN` when chasing UB.

## Coding Style & Naming Conventions
- Target C++20 with `#pragma once` headers, concepts, and `tl::expected` until C++23 adoption.
- Prefer snake_case for namespaces/functions, PascalCase for templates, and `UPPER_SNAKE_CASE` for constants and macros.
- Keep encoder/decoder overloads noexcept where practical; rely on RAII and value semantics for ownership.
- Run clang-tidy targets before submitting large refactors; mirror existing formatter settings if you script clang-format.

## Testing Guidelines
- Tests rely on doctest; each scenario belongs in a dedicated `TEST_CASE` with descriptive, lowercase titles.
- Add regression coverage beside related code (e.g., new container support → `test_ranges.cpp`).
- Use `ctest` for CI parity and `./build/test/tests --test-case="pattern"` to focus on one failure locally.
- Maintain fast unit tests; move long-running checks into `benchmarks/`.

## Core One-Shot Decoder Segment Contract

This boundary is intentional. Do not reinterpret it while changing decoder or
codec code:

- The caller owns the input buffer: its lifetime, framing/admission limits, and
  a stable admitted `[begin, end)` range for the call. The caller also owns the
  truthfulness of its C++ range semantics, including any
  `std::ranges::sized_range` extent promise.
- The library owns parsing a declared CBOR segment from that supplied buffer.
  Here, "segment" means the next requested CBOR item(s), including a definite
  item's header-declared payload; it is not transport framing or validation of
  arbitrary trailing input. A supported input range need not model
  `std::ranges::sized_range`.
- For an unsized non-contiguous input, core typed decoding consumes a declared
  payload or container exactly once. Do not add a prewalk, scanner, `distance`,
  or other traversal merely to prove that bytes declared by a CBOR header exist.
- If that one traversal reaches the end, return `status_code::incomplete`.
  One-shot decoding is terminal: do not roll back the reader or destination;
  a decoded prefix may remain.
- Never reserve from a CBOR length header alone. For definite strings, reserve
  only after an exact range-provided availability check from contiguous input
  or input that models `std::ranges::sized_range`, without an explicit decoder
  prewalk. For arrays/maps, retain only the existing one-byte/two-byte-per-entry
  lower-bound guard before reservation; it is not a full payload check.
- This is the core typed-decoder rule. An extension may expose its own
  documented terminal availability/status boundary, but that does not authorize
  a direct-reader prewalk for recovery, ordinary decode validation, or rollback.
  A scanner is a separate semantic feature, not a generic extension exception:
  it needs an explicit design, documentation, and tests (for example, CDDL or
  lazy-tag discovery). It must remain terminal, return an explicit status, and
  must not be generalized back into core decoding.

## Commit & Pull Request Guidelines
- Follow short, imperative commit subjects (e.g., `Fix decode overflow guard`), mirroring recent history.
- Group logically complete changes; avoid drive-by formatting unless it unblocks the patch.
- PRs should explain intent, list key changes, and note any follow-up TODOs; link issues and attach perf/test evidence when relevant.
- Confirm `ctest` passes and mention optional clang-tidy or sanitizer runs in the PR description when used.
