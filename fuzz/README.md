# Linux fuzz testing

[Google FuzzTest](https://github.com/google/fuzztest) with its built-in engine, GoogleTest and ASan.
Requires CMake 3.25+, Ninja, Clang, matching LLVM coverage tools, Python 3.11+ and ripgrep.

```bash
cmake -S fuzz -B build/fuzz -G Ninja \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCBOR_FUZZ_COVERAGE=ON
cmake --build build/fuzz --parallel 4
ctest --test-dir build/fuzz --output-on-failure
python3 fuzz/run_campaign.py build/fuzz/cbor_fuzz --seconds 60 --jobs 4 \
  --corpus build/fuzz-corpus --output build/fuzz-evidence
bash fuzz/coverage.sh build/fuzz/cbor_fuzz build/fuzz-evidence
```

The budget is per property. Reuse the corpus; give each run a new output directory.
Use `--filter PROPERTY` to select a property. Logs, tool/source hashes and LLVM
coverage reports go in the output directory.

## Sources

- Properties: [round trips](roundtrip_fuzz.cpp), [wire](wire_fuzz.cpp),
  [extensions](extensions_fuzz.cpp), [visualization](visualization_fuzz.cpp).
- Inputs: [typed domains](fuzz_support.h), [wire domains](wire_domains.h),
  [CBOR vocabulary](cbor.dict).
- [Build and dependencies](CMakeLists.txt), [CI](../.github/workflows/fuzz.yml).
