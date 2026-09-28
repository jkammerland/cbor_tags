# Local validation — 2026-09-28

Tested on Linux x86_64 with Fedora Clang 22.1.8, LLVM coverage tools 22.1.8,
CMake 4.4.2, Ninja 1.14.0.git, and Python 3.11.13. The worktree was based on
`b751b0d` (0.25.0). Both external-checkout and default FetchContent builds were
validated with C++20, `RelWithDebInfo`, ASan, and LLVM source coverage.

## Results

- Repository Debug build and CTest: **84/84 passed**.
- Standalone CTest: **2/2 passed**, comprising 31 fuzz properties, two wire smoke
  tests and three Python tooling regression tests.
- Repository format and clang-tidy gates, Ruff, actionlint, and shell syntax:
  passed.
- Initial campaign: 26 properties × 60 seconds, four processes;
  **62,781,728 executions**, no failures.
- Final expanded campaign: 31 properties × 30 seconds, four processes, replaying
  the saved corpus; **40,333,308 executions**, no failures.
- Both campaigns used `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`; neither
  produced an ASan or leak report. No library implementation changes were needed.

The final command was:

```bash
python3 fuzz/run_campaign.py build/fuzz/cbor_fuzz --seconds 30 --jobs 4 \
  --corpus build/fuzz-corpus --output build/fuzz-evidence-final
bash fuzz/coverage.sh build/fuzz/cbor_fuzz build/fuzz-evidence-final
```

## Final campaign source coverage

| Library source | Regions | Lines | Branches |
| --- | ---: | ---: | ---: |
| `cbor_decoder.h` | 89.60% | 87.86% | 75.19% |
| `cbor_encoder.h` | 91.23% | 94.63% | 85.71% |
| `cbor_traversal.h` | 91.00% | 95.91% | 82.76% |
| `detail/cbor_item.h` | 94.99% | 92.92% | 93.63% |
| `detail/cbor_utf8.h` | 100.00% | 100.00% | 100.00% |
| `cbor_lazy_tags.h` | 93.18% | 85.71% | 75.00% |
| `extensions/rfc8746_typed_arrays.h` | 98.44% | 98.73% | 91.67% |
| `extensions/cbor_visualization.h` | 84.03% | 68.35% | 77.36% |
| `cbor_segments.h` | 74.63% | 60.51% | 71.19% |
| All emitted library source | 85.58% | 70.09% | 79.64% |

The total contains 2,621 regions, 4,804 lines and 1,488 branches. It excludes
framework, harness and standard-library code. These are the template
instantiations emitted by this executable, not a claim about all possible
schemas or configurations. Reflection/concept diagnostics include compile-time
code with low runtime line coverage. CDDL variants, segment helper overloads,
shared-pointer policies, and binary16 conversion helpers also leave runtime
coverage gaps. Allocation-failure injection and alternative reflection backends
remain in the ordinary repository tests. Longer campaigns and additional
schemas can extend the persisted corpus and coverage.

## Provenance and artifacts

- FuzzTest: `7a8e9056de2a4f745082a4b3861a67ada47ced36`.
- GoogleTest: `52eb8108c5bdec04579160ae17225d66034bd723`.
- Abseil: `5650e9cf76d3be4318d5fa3af38ee483ddfd5e4a`.
- RE2: `927f5d53caf8111721e734cf24724686bb745f55`.
- Compiler executable SHA-256:
  `4ea85504401d9f5c05109d6cb05778b241aa8d56fde1da2e5bc7ba46cffd44f5`.
- Final fuzz binary SHA-256:
  `fc47508afda9514d2ebb9a8cdf5d3c29ed53064ee557394099f58bfa5290bdd6`.

The local evidence is under
`/tmp/cbor-tags-fuzz/build/fuzz-evidence-final/`: `summary.json`, per-property
logs, raw/merged profiles, `coverage.txt`, and `html/index.html`. The saved corpus
is in the sibling `fuzz-corpus/` directory. CMake cache, compile commands, tool
hashes and source hashes are recorded there. CI uploads equivalent artifacts.
Hashes identify this run; they do not require restoring historical executables.
