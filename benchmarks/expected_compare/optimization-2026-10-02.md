# Expected optimization, 2026-10-02

Scalar copy/move assignment and failed `value()` access now match tl's runtime
in the measured Clang workloads. Expected-only object code decreases with both
compilers. The benchmark baseline is commit `3f086bb`, whose production expected
header is the 0.26.0 release header.

## Implementation

When both alternatives are unqualified scalars, copy/move assignment uses the
internal union's trivial assignment followed by the state flag. This removes
branches on the destination's previous state:

```cpp
data_ = std::move(other.data_);
has_ = other.has_;
```

The union assignment also starts the matching nested member's lifetime, as
specified in C++23 [class.copy.assign, paragraph 13](https://timsong-cpp.github.io/cppwp/n4950/class.copy.assign#13).
Class payloads retain memberwise assignment and construction/destruction:
even a trivial class can contain volatile members with observable accesses.
Public assignment constraints, exception specifications, and nontrivial
assignment traits retain the [C++23 contract](../../doc/expected.md).

GCC/Clang's `always_inline` attribute on the throw helper removes its extra
unwinder frame. Clang previously called `impl::bad_access` from `value()`;
it now emits the exception allocation/construction/throw in the caller.
Error forwarding and the `bad_expected_access<void>` base remain intact.

## Runtime

GCC 16.2.1 and Clang 22.1.8, C++20, `-O3`, no LTO, Ryzen 9 7950X.
Five paired rounds compare the baseline, optimized implementation, and tl 1.3.1
at four section placements each. Processes run on CPU 2. Measurements retain
the existing noinline producer boundary, compiler barriers, independent
arithmetic checks, and typed codec fixtures. Each row is a median over 20
samples; the machine's frequency scaling and shared caches remain active.

Times below are nanoseconds per item; codec times are per container.

| Compiler and workload | Baseline | Optimized | tl | Change from baseline |
|---|---:|---:|---:|---:|
| Clang scalar assignment, alternating states | 2.444 | 1.165 | 1.166 | -52.3% |
| Clang `value()`, 50% errors | 223.822 | 175.655 | 176.066 | -21.5% |
| Clang `value()`, one error in 64 | 8.178 | 6.506 | 6.611 | -20.4% |
| Clang string assignment, alternating states | 5.841 | 5.811 | 4.871 | -0.5% |
| GCC scalar assignment, alternating states | 1.250 | 1.183 | 7.292 | -5.3% |
| GCC `value()`, 50% errors | 181.519 | 181.284 | 189.236 | -0.1% |
| GCC decode, 4096 integers | 8955.261 | 8956.478 | 8955.213 | +0.0% |
| Clang decode, 4096 integers | 10928.780 | 10908.108 | 10939.627 | -0.2% |

Clang's alternating-assignment improvement holds at every placement: the
optimized/baseline median ratios span 0.405–0.552. Failed `value()` ratios span
0.776–0.789. GCC's assignment result varies with placement (0.851–1.077), so its
aggregate improvement is weaker evidence. Successful scalar return and monadic
workloads are essentially flat. Clang string assignment retains a roughly 19%
gap against tl in the alternating workload; its instruction body is unchanged.

### Encoder placement

The combined executable's encoder medians increased by 6–8% with GCC and
13–14% with Clang. The four-placement sweep moves the exported kernels while
some codec helpers stay in the ordinary text section. Their absolute addresses
also change when expected code shrinks. For example, Clang's hot
`append_cbor_major_argument` helper moves from `0x400a60` to `0x400a30`.

After removing alignment no-ops and relocation addresses, encoder instructions
match between the two combined objects. An isolated encoder harness compiled
against the release and optimized headers produces byte-identical objects and
four byte-identical executable pairs with both compilers. Three paired timing
rounds of those isolated builds differ by -0.6% for GCC and +0.2% for Clang.
These results support code placement as the cause of the combined encoder
shift. The combined executable's slowdown is still part of the measured result;
this optimization does not establish a general encoder speedup.

## Code size

Expected-only object code includes emitted helpers and alignment instructions,
with codec kernels excluded. Debug data and dynamic-library code are excluded.

| Compiler | Optimization | Baseline | Optimized | tl |
|---|---|---:|---:|---:|
| GCC | `-O2` | 2322 B | 2274 B | 2312 B |
| GCC | `-O3` | 2354 B | 2306 B | 2360 B |
| GCC | `-Os` | 1187 B | 1155 B | 1290 B |
| Clang | `-O2` | 1713 B | 1596 B | 1580 B |
| Clang | `-O3` | 1713 B | 1596 B | 1580 B |
| Clang | `-Os` | 1526 B | 1466 B | 1465 B |

At `-O3`, expected-only linked code decreases from 2908 to 2860 bytes with GCC
and from 2287 to 2167 bytes with Clang. Clang linked unwind metadata decreases
from 1012 to 968 bytes; GCC stays at 860 bytes. Linked read-only data stays at
344 bytes for GCC and 255 bytes for Clang. The linked driver and runtime stubs
are included in these totals. Size, alignment, and special-member traits remain
identical for the eight payload combinations in the layout probe.

## Validation and evidence

- All 134 Clang Debug CTest checks pass, including expected contracts, C++23
  standard parity, invalid-program diagnostics, and no-exception execution.
- GCC C++20 expected tests pass. Clang C++23 expected tests pass under ASan/UBSan.
- New cases exercise scalar and aggregate state transitions, self-assignment,
  pointers, void/cv-void values, constructor/destructor effects, and throwing
  error copies/moves during failed checked access.
- The two optimized fixture executables pass at all six size configurations.
- Repository formatting and clang-tidy pass.

Local evidence is under `build/expected-optimization/`: the final report,
raw samples, sizes, disassembly, compiler commands, executable hashes, and tool
versions/hashes are in `final/`. `measure-revisions.py` records the paired run;
`encode-isolation.json` records the isolated encoder commands and hashes.
Baseline executables are used in their original build directories. The final
scalar-only restriction was followed by a rebuild of all six configurations;
kernel objects and all measured runtime executables retained identical SHA256
hashes, so those runtime samples remain applicable.

SHA256 of the measured inputs and summaries:

```text
baseline expected.h  ee6fa33b5f6d62f142897a64baae5ebef19e4862e4bbc3b1adad24c724fd4b2c
optimized expected.h 60bf7ac4655326e2c81b6c2726a2e0403b714e14d5f94bf7ccc7d6cd7665af55
final/runtime.json   c8e25dbc6af4260035f259bc74a595931629dc4563ea694e42b4e7090783aba0
final/sizes.json     7b12c35820d0145b5833cea6601c5558a43821efaf958ddb7ad4a3c594082f21
final/samples.json   8c8db622a0d19e9cb570bf78e6634fe8ce688fa9c5695fec71a56f177e56a8dd
```

To repeat the current implementation's comparison against tl without
overwriting the baseline artifacts:

```bash
python3 scripts/compare-expected.py \
  --tl-include /path/to/tl-expected/include \
  --cpu 2 \
  --output build/expected-optimized-comparison
```
