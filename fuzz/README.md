# Linux fuzz testing

[Google FuzzTest](https://github.com/google/fuzztest) with Centipede, GoogleTest and ASan.
Requires Bazelisk, Clang, matching LLVM coverage tools, Python 3.11+ and ripgrep.

```bash
cd fuzz
bazelisk test --config=coverage //:cbor_fuzz
python3 test_tools.py
python3 run_campaign.py bazel-bin/cbor_fuzz --seconds 60 --jobs 4 \
  --corpus ../build/fuzz-corpus --output ../build/fuzz-evidence
bash coverage.sh bazel-bin/cbor_fuzz ../build/fuzz-evidence
```

The budget is per property. Reuse the corpus; give each run a new output directory.
Use `--filter PROPERTY` to select a property. Logs, tool/source hashes and LLVM
coverage reports go in the output directory.

## Sources

- Properties: [round trips](roundtrip_fuzz.cpp), [wire](wire_fuzz.cpp),
  [extensions](extensions_fuzz.cpp), [visualization](visualization_fuzz.cpp).
- Inputs: [typed domains](fuzz_support.h), [wire domains](wire_domains.h),
  [CBOR vocabulary](cbor.dict).
- [Build](BUILD.bazel), [engine flags](.bazelrc), [dependencies](MODULE.bazel),
  [CI](../.github/workflows/fuzz.yml).
