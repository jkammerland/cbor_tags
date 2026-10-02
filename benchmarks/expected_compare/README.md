# Expected comparison

Requires Linux, Python 3, GCC, Clang, CMake, Ninja, GNU binutils, and `taskset`.
From the repository root, point `--tl-include` at the directory containing `tl/expected.hpp`:

```bash
python3 scripts/compare-expected.py \
  --tl-include /path/to/tl-expected/include
```

Results are written to `build/expected-comparison/`.
