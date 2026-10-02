# Expected comparison

This standalone project compares the checked-out built-in expected implementation
with `tl::expected` 1.3.1. The initial measurements used 0.26.0.
Both executables compile the same kernels and
codec headers. The tl executable uses a generated include override for the
public expected aliases; each executable uses one backend consistently.

Use normally installed GCC, Clang, CMake, Ninja, and GNU binutils. Supply an
installed or externally cached tl header directory; dependencies are not copied
into the repository. For example:

```bash
python3 scripts/compare-expected.py \
  --tl-include /path/to/tl-expected/include \
  --cpu 2
```

The script builds both backends at `-O2`, `-O3`, and `-Os`, runs fixture checks
through CTest, and writes results under `build/expected-comparison/`. Runtime
measurements use `-O3`, five alternating rounds, and four section placements.
Use `--compiler /usr/bin/clang++` to select one compiler, `--runs` to change the
repetition count, or `--output` to keep another run separately.

`layout.cpp` measures object size, alignment, and triviality for eight common
payload combinations. `kernels.cpp` covers materialized void results, scalar
returns across a function boundary, monadic chains, state assignment, nontrivial
string values, checked access, and encoder/decoder returns. Materialized rows
force the object's representation to memory with a compiler barrier. String
successes allocate a 32-character value. The `half_error` pattern alternates
success and failure; `one_error_in_64` has one failed result per 64 items.

The driver checks arithmetic results against plain-value calculations before
timing. Encoder buffers have enough reserved capacity. Decoder destinations
are cleared while retaining capacity, and codec fixtures compare decoded typed
values with their sources. The benchmark is not a full API compatibility suite;
our C++23 constraints and tl's extended API differ.

Code-size results include an expected-only build that excludes codec functions,
plus the combined codec corpus, exported function bodies, and linked
code, read-only data, and exception/unwind metadata. Debug sections are excluded.
The linked code also includes the common validation driver and startup stubs;
external shared-library code is excluded. Per-function bytes exclude called
helpers, so compare them together with the object totals. Stripped executable
file sizes are recorded in `sizes.json`, but contain file layout and alignment
padding and should not be treated as executable instruction counts.

The relocation sweep samples code placement sensitivity. Identical section
starts do not guarantee identical inner-loop alignment between backends because
function sizes differ. Read each layout's ratios alongside the aggregate median.
CPU affinity does not isolate cache sharing or frequency scaling.

The first run on 2026-10-02 used a Ryzen 9 7950X, GCC 16.2.1 and Clang 22.1.8.
Object sizes and alignments matched for all measured combinations. The built-in
implementation's scalar assignments were nontrivial, while tl's were trivial;
our implementation currently follows the C++23 assignment rules documented in
`doc/expected.md`.

At `-O3`, the expected-only sizes were:

| Compiler | Object code, ours/tl | Linked read-only data, ours/tl | Linked unwind metadata, ours/tl |
|---|---:|---:|---:|
| GCC | 2354 / 2360 B | 344 / 184 B | 860 / 812 B |
| Clang | 1713 / 1580 B | 255 / 140 B | 1012 / 880 B |

GCC's expected-only instruction footprint was nearly equal. Clang emitted 133
more instruction bytes for ours (8.4%). Read-only data and unwind metadata also
increased; these include exception messages, type information and landing-pad
metadata. The measured corpus and shared-library boundary define these sizes.

The first run found Clang leads in alternating scalar assignment and failed
`value()` access: the built-in implementation took about twice as long for the
former and about 28% more time for the latter across all four placements.
String assignment also took 6–19% more time in the measured patterns. GCC
scalar-return paths favored our implementation: its producer returned directly
in registers, while tl's emitted stack stores and loads. These are results for
the measured function boundaries and workloads, not general expected rankings.
Core codec decoding was essentially flat. Encoder ratios varied with placement.
The detailed measurements and source/tool hashes are in the generated report,
CSV files, `sizes.json`, and `provenance.json`.

The [optimization follow-up](optimization-2026-10-02.md) measures scalar union
assignment and an inlined bad-access helper against that baseline. Clang's
alternating assignment improved by 52%, failed checked access by 21%, and
expected-only object code decreased from 1713 to 1596 bytes. The follow-up also
records GCC results and the encoder placement investigation.
