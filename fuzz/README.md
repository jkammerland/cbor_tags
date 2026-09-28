# Linux FuzzTest suite

This standalone CMake project uses [Google FuzzTest](https://github.com/google/fuzztest)
properties and GoogleTest assertions. Normal library builds and the doctest
suite do not fetch or link FuzzTest. Linux and Clang are required.

FuzzTest's CMake integration currently uses its in-process engine. Upstream's
[Centipede integration](https://github.com/google/fuzztest/tree/main/centipede)
uses Bazel; this suite does not claim to run Centipede. The property API,
structured domains, shrinking, comparison feedback, and ASan work with the
supported CMake engine.

## Build and smoke test

Use a normally installed Clang toolchain, matching `llvm-cov`/`llvm-profdata`,
CMake 3.25+, Ninja, Git, Python 3.11+, and ripgrep. The FuzzTest source revision
is pinned in CMake and fetched into the build directory. Its own dependency
manager fetches GoogleTest, Abseil, RE2 and ANTLR. No dependencies or toolchain
binaries are vendored in the repository.

```bash
cmake -S fuzz -B build/fuzz -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCBOR_FUZZ_COVERAGE=ON
cmake --build build/fuzz --target cbor_fuzz --parallel 4
ctest --test-dir build/fuzz --output-on-failure
```

An existing external checkout can be selected with
`-DCBOR_FUZZTEST_SOURCE_DIR=/absolute/path/to/fuzztest`. All framework targets
use C++20 to match the library and avoid Abseil's language-mode-dependent ABI.
`FUZZTEST_FUZZING_MODE=ON` is the default and applies upstream's ASan and
coverage flags. For quick uninstrumented property tests, configure a separate
directory with `-DFUZZTEST_FUZZING_MODE=OFF`; continuous fuzzing requires it ON.

## Run a campaign

```bash
python3 fuzz/run_campaign.py build/fuzz/cbor_fuzz \
  --seconds 60 --jobs 4 \
  --corpus build/fuzz-corpus --output build/fuzz-evidence
bash fuzz/coverage.sh build/fuzz/cbor_fuzz build/fuzz-evidence
```

The budget is **per property**, and `--jobs` limits independent processes.
Use a new output directory for every run; reuse the corpus directory to resume.
Each property gets its own serialized FuzzTest corpus, log and timeout. A failed
property or timeout makes the campaign fail; there are no retries. Other
properties still finish so their evidence is retained. ASan defaults to
`detect_leaks=1:halt_on_error=1`; an explicitly set `ASAN_OPTIONS` is recorded.

`summary.json` records commands, exit codes, durations, source hashes, dependency
revisions, and tool versions/hashes. CMake cache and compile commands are copied
as provenance. They describe the run, not a requirement to reuse old executables.
Keep corpora, logs and HTML reports as CI artifacts rather than source files.

To select or reproduce a single property:

```bash
build/fuzz/cbor_fuzz --list_fuzz_tests=1
python3 fuzz/run_campaign.py build/fuzz/cbor_fuzz --filter CborWire.raw_item_view \
  --seconds 300 --corpus build/fuzz-corpus --output build/fuzz-recheck
FUZZTEST_REPLAY=/absolute/path/to/serialized-input \
  build/fuzz/cbor_fuzz --fuzz=CborWire.raw_item_view --corpus_database=
```

FuzzTest also prints a reproduction command when an assertion fails. Preserve
that input, diagnose the contract violation, and add a focused regression test.
Do not relax assertions to silence a finding. FuzzTest corpus files are typed
serialized inputs; arbitrary `.cbor` files are not interchangeable with them.

## Properties and vocabulary

| Area | Inputs and checks |
| --- | --- |
| Scalars | Signed/unsigned integers, full negative range, binary16/32/64, NaNs and signed zero; exact semantic round trips |
| Structured values | Tagged records, optional fields, variants, nested maps/vectors, UTF-8 and binary strings; decode equality and full segment consumption |
| Admission and truncation | Strict integer representability, dynamic string bounds, encoded record prefixes returning `incomplete` |
| Raw typed decoding | Arbitrary bytes decoded as scalars, fixed arrays, bounded strings/arrays/maps, records and variants; successful results agree across supported range categories |
| Raw views and validation | Exact borrowed item extent and preserved bytes; strict validation agrees across contiguous and segmented inputs |
| Extensions | RFC 8746 integer and float arrays in both byte orders, malformed typed-array data, non-recursive smart pointers, lazy tag discovery and payload decoding |
| Segments | Segmented record encoding, owned/borrowed storage, copies/moves, append and flatten semantics |
| Visualization | Diagnostic/annotation results and failures agree across buffer types; valid structured input renders; CDDL formatting options preserve schema names/types across output containers |

`cbor.dict` is a 357-token libFuzzer/Centipede-compatible vocabulary, compiled
into the harness by `generate_vocabulary.py`. Raw-byte domains use it both as
mutation dictionary entries (`WithDictionary`) and initial seeds (`WithSeeds`).
It includes all major types; argument-width boundaries; reserved additional
info; definite/indefinite strings, arrays and maps; tag and typed-array headers;
UTF-8 edge cases; floating-point encodings; and malformed/truncated fragments.
GoogleTest additionally exercises all 256 initial bytes deterministically.
Typed domains get FuzzTest's numerical boundary mutations and bounded container
generation, so valid payloads reach beyond header rejection paths.

## Resource and decoder contracts

Raw input is capped at 4 KiB; structured strings at 128 bytes, binary payloads
at 256 bytes, vectors at 64 values, and maps at 16 entries. The raw container
property adds explicit size bounds; other fixed schemas are constrained by the
admitted input. The schemas do not introduce application recursion.
Visualization/validation depth is bounded. Input storage remains alive and
stable for each call, including all borrowed views and lazy payload decoders.

An unsized range is a standard bidirectional list-iterator subrange. The harness
does not scan input to validate a header before calling the decoder. Failure
may consume a prefix or modify the destination. Range comparisons require equal
successful values, without requiring identical failure offsets or status
precedence where availability checks intentionally differ. Trailing input is
not rejected merely because the requested segment is complete.

## Coverage and CI

`coverage.txt` and `html/index.html` report **library** source coverage, excluding
the harness, framework and STL. The denominator includes code emitted for the
selected template instantiations; it is not every possible schema or library
configuration. Fuzzer edge counts are a separate signal and include harness code.
No percentage is presented as proof of correctness.

The Linux workflow runs smoke checks and ten seconds per property on PRs, with
sixty seconds per property for scheduled/manual runs. It persists the corpus
and uploads logs, source coverage, and provenance even after a campaign failure.
Windows/macOS consumers remain unaffected. Compile-time reflection backends,
application-defined codecs and allocation-failure injection remain covered by
the ordinary repository tests rather than this runtime fuzz suite.
