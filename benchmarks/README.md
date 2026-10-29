# Benchmarks

These benchmarks are intended to track codec behavior over time. Keep existing
benchmark bodies stable unless a benchmark is clearly measuring setup work
instead of the path named in its row.

## Reflection compile stress

The reflection compile benchmark instantiates 512 unique eight-member aggregates
through the generated C++20 fallback. It measures value and mutable-reference
aggregates separately with ccache disabled:

```bash
scripts/benchmark-reflection-compile.sh g++ clang++
```

Set `CBOR_TAGS_REFLECTION_BENCH_INCLUDE_DIR` to compare another checkout, or set
`CBOR_TAGS_REFLECTION_BENCH_INSTANCES` and `CBOR_TAGS_REFLECTION_BENCH_RUNS` to
change the workload and repetition count. The script reports every wall-time and
peak-RSS sample without enforcing a pass/fail threshold.

The harness normally verifies an arity of eight. When measuring an older broken
baseline that reflected mutable-reference aggregates as empty, set
`CBOR_TAGS_REFLECTION_BENCH_REFERENCE_EXPECTED_ARITY=0`.

## Decoder `CHECK(...)` Rows

Rows such as `Decoding a uint with check` use doctest assertions:

```cpp
CHECK(dec(value));
```

Those rows measure doctest assertion machinery plus expected-result truth
checking in addition to decode work. They are useful as a test-like smoke path,
but they should not be used as raw codec performance evidence.

For codec performance, prefer rows that either ignore the returned status or
explicitly materialize it without doctest:

```cpp
auto result = dec(value);
ankerl::nanobench::doNotOptimizeAway(result);
```

The existing decoder benchmarks also prepare some payloads from
`std::random_device`. Before making release-to-release performance claims, add
or use fixed-payload benchmarks so each version decodes the same CBOR integer
widths and container contents.

## Typed Decode Preflight Regression

`bench_decode_preflight.cpp` uses fixed payloads designed to expose a whole-item
structural pass before typed decoding:

- definite arrays containing 32 and 4096 one-byte unsigned integers;
- an indefinite array containing 4096 one-byte unsigned integers;
- 128 nested one-element arrays decoded through a recursive codec.

Each table pairs `public direct typed decode` with
`emulated structural preflight + typed decode`. The emulation exists only in the
benchmark and uses the internal raw-item skipper before invoking the public
decoder. It must not be moved into the normal decode path.

Run only this fixture in a Release benchmark build:

```bash
cmake -S . -B build/benchmarks \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCBOR_TAGS_BUILD_BENCHMARKS=ON \
  -DCBOR_TAGS_BUILD_TESTS=OFF
cmake --build build/benchmarks --target bench_decoder --parallel
build/benchmarks/benchmarks/decoder/bench_decoder \
  --test-case="decode preflight regression benchmarks" --no-skip
```

The following directional results compare `dd71501`, which adds the real root
preflight to parent `6617765`, with direct decoding at that parent/current
implementation. Both builds used GCC 15.2.1, `-O3 -DNDEBUG`, one pinned CPU,
15 epochs of at least 20 ms per row, and two interleaved runs:

| Fixed payload | Preflight, ns/byte | Direct, ns/byte | Direct speedup |
|---|---:|---:|---:|
| definite uint array, 32 elements | 3.26-3.52 | 0.87 | 3.7-4.0x |
| definite uint array, 4096 elements | 2.31 | 0.79-0.80 | 2.9x |
| indefinite uint array, 4096 elements | 2.27 | 0.67 | 3.4x |
| nested one-element arrays, depth 128 | 4.37-4.38 | 1.03-1.04 | 4.2x |

The host reported CPU frequency scaling and the `powersave` governor, so these
are regression-direction measurements rather than release claims. As a
cross-check, the current emulated-preflight rows stayed close to the historical
public-preflight rows.

## Shared Pointer Encode Rows

The encoder suite includes `shared_ptr encode N unique x2` rows. Each row
encodes a `std::vector<std::shared_ptr<std::uint64_t>>` containing `N`
first-seen pointers followed by the same `N` pointers again. The benchmark uses
one fresh `cbor::tags::codec::shared_ptr` encoder per iteration, including its internal
address-and-pointer-type lookup.

Lookup strategy and reservation are deliberately not public options. An
application that needs different storage can supply its own scope through
`set_shared_ptr_scope(...)` and benchmark that scope in the application.

## Custom Codec 1 vs Default CBOR

`bench_custom_codec_1` compares the default CBOR codec with the `custom_codec_1`
extension on fixed tagged payloads:

- an inline-tagged aggregate record with integers, floats, bytes, text, and a
  numeric sample vector;
- explicitly tagged `std::vector<double>` and `std::vector<float>` fixtures;
- RFC 8746 typed-array encodings of the same homogeneous numeric fixtures.

The encode rows reuse an output buffer reserved to the known encoded size, so
they focus on codec work instead of allocation or output growth policy. The
decode rows use pre-encoded default-CBOR and `custom_codec_1` payloads. A
non-benchmark fixture test also verifies that both codecs round-trip the same
user values and captures the encoded byte sizes.

The suite also includes wire-throughput rows with `unit("byte")` and
`batch(encoded_size)`. Those rows report absolute encoded-byte throughput for
the tagged record and for tagged `std::vector<double>` and `std::vector<float>`
fixtures with 16, 1024, and 65536 elements. The same vector fixtures also
include RFC 8746 typed-array rows, which encode homogeneous numeric values as
standard CBOR typed-array tags over a byte-string payload. For same-value
comparisons, read these together with the latency rows because default CBOR,
RFC 8746 typed arrays, and `custom_codec_1` do not always emit the same number
of bytes.

Do not read these rows as evidence that the default codec is slow for every
large payload. Definite `bstr` and `tstr` values are already favorable in the
default codec: they encode as a CBOR length header plus one payload append for
contiguous byte/text inputs, and contiguous-input decode can either bulk-copy
into an owning container or bind a view over the original input. The main
default-CBOR cost that `custom_codec_1` and RFC 8746 typed arrays avoid for
homogeneous numeric vectors is the per-element array item encoding, not byte or
text string payload movement.

Rows named `custom_codec_1 zc ... encode segment assembly (represented bytes)` use
`encode_borrowed_segments(...)`. They assemble the same outer `tag(bstr)` wire
shape as normal `custom_codec_1` encoding, but the numeric payload bytes are
borrowed from the source vector instead of copied into an owned destination.
Those rows measure segment assembly throughput. The `byte/s` number is based on
represented wire bytes and does not include payload flattening or a final write
to a contiguous destination.

Rows named `rfc8746 typed array ... encode` use the public
`cbor::tags::codec::typed_array` with an owned byte-vector destination. On
little-endian hosts the typed-array payload is appended from the native
contiguous vector bytes, so the hot path is header work plus one payload append.
Rows named `rfc8746 typed array zc ... encode segment assembly (represented bytes)` use
`rfc8746::encode_typed_array_borrowed_segments(...)`; on little-endian hosts the payload
segment is borrowed from the source vector. Decode rows are split between
`decode owning`, which materializes a `std::vector<T>`, and `decode borrowed`,
which decodes a `typed_array_view<T>` over the input payload without copying the
values. Borrowed decode row names include `view bind (represented bytes)` when
the measured work only parses the header and binds a view over the original
payload.

## Serialization Comparison Suite

The cross-library comparison suite is opt-in because it fetches and builds extra
dependencies. It compares `cbor_tags`, bitsery, zpp_bits, cereal, Glaze CBOR,
Boost.Serialization, and FlatBuffers across fixed fixtures, then writes a
Markdown report plus raw nanobench JSON/CSV.

Run it with:

```bash
scripts/run-comparison-benchmarks.sh
```

The default output is `build/benchmark-comparison/report/`. Pass a path as the
first argument, or set `CBOR_TAGS_BENCHMARK_REPORT_DIR`, to write the report
elsewhere. The generated report calls out wire-format, schema, decode-behavior,
and allocation differences so the numbers are not mistaken for like-for-like
protocol equivalence.

Encode allocation reporting is split into cold and reserved destination paths.
Cold encode rows start from an empty byte buffer and include output growth.
Reserved encode rows reserve the final encoded byte length before measurement
and count only additional encode-time allocations.

The suite keeps known-type rows separate from `*_enveloped` rows. Enveloped
rows add benchmark-local top-level type detection: `cbor_tags` uses a top-level
tag, Glaze CBOR uses a two-element array, FlatBuffers uses a file identifier,
and binary archives prefix a `uint32_t` object id. Homogeneous numeric fixtures
also include CBOR RFC 8746 typed-array rows using IANA tags 78, 79, and 86 for
little-endian int32, int64, and float64 byte-string payloads. Glaze CBOR uses
that typed-array wire shape for numeric vectors by default. Those rows are
intentionally not wire-equivalent to generic CBOR arrays. The typed-array rows
include the public `cbor_tags` RFC 8746 codec and Glaze CBOR paths, plus
benchmark-local byte-range, contiguous bulk-copy buffer, and borrowed
header-plus-payload segment paths so range overhead and the zero-copy ceiling
are visible separately. The byte-range and bulk-copy rows are encode variants;
their decode validation uses the public `cbor_tags` typed-array codec.

## Resumable throughput and completion latency

Build the native targets in Release mode:

```bash
cmake -S . -B build/resumable-bench -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCBOR_TAGS_BUILD_BENCHMARKS=ON \
  -DCBOR_TAGS_BUILD_TESTS=OFF
cmake --build build/resumable-bench \
  --target bench_resumable_throughput bench_resumable_latency --parallel
ctest --test-dir build/resumable-bench -R 'resumable_(throughput|latency|allocation)' --output-on-failure
```

`bench_resumable_throughput` compares ordinary encode/decode with resumable
encode/decode supplied as a complete buffer or in 1460, 4096, and 65536 byte
chunks. Separate validation rows compare `validate_item` with the resumable
validator, without creating decoded values. Deterministic fixtures include
nested records, definite/indefinite scalar arrays and ordered/hash maps of integer
pairs, `float`/`double`/`bool` arrays, float-key/bool-value maps,
native/opposite-endian RFC 8746 uint32 arrays, and definite/indefinite
text and binary strings. Boolean fixtures use `deque<bool>` to provide ordinary
references rather than packed proxies. Floating fixtures use exact finite binary
fractions. Nested records contain a 4096-character string and
64 integers spanning CBOR integer widths. Text fixtures contain ASCII; the
separate `tstr-utf8` workload alternates one-, two-, three-, and four-byte code
points. Integer values repeat a fixed width-spanning pattern. These workloads
do not characterize every UTF-8 or value distribution. The default fixture budget is
16 MiB; array/map/typed-array wire sizes vary with their representation, and every row
prints the actual encoded byte count. Typed decoding uses owning `typed_array`
destinations; encoding borrows the same vector values through typed wrappers.

```bash
build/resumable-bench/benchmarks/resumable/bench_resumable_throughput
build/resumable-bench/benchmarks/resumable/bench_resumable_throughput \
  --bytes 16777216 --repetitions 3 --epochs 9 --workload structured

# Change CBOR string chunks independently of transport chunks.
build/resumable-bench/benchmarks/resumable/bench_resumable_throughput \
  --workload bstr-indef --cbor-chunk 65536 --transport 1460 \
  --storage both --direction decode --bytes 1048576

# Count destination allocations outside timing, including a reserved comparison.
build/resumable-bench/benchmarks/resumable/bench_resumable_throughput \
  --workload unordered-map --allocations --bytes 65536
```

Each round reverses mode order. Nanobench runs nine epochs of at least 20 ms
per row by default. Output includes every row's encoded MB/s, ns/byte, epoch
minimum/maximum, iteration count, and a median/range across the three rounds.
Decode rows create fresh destinations, including allocation and destruction of
the previous destination. `--storage reserved|both` adds `decode-reserved` rows
for containers with `reserve()`: capacity is supplied before timing, while
clearing previous elements remains timed. For unordered maps this reserves
buckets; node allocations remain. Ordered maps have no reserved row. Ordinary
owning typed-array decode replaces its vector and discards the offered capacity,
while resumable decode reuses it. Its reserved rows therefore compare the API
with offered capacity, not equal allocation behavior; cold rows are the primary
typed comparison. Encode rows reuse equally sized caller-provided output storage. Operation creation, source descriptors, and chunk feeding are
included. Fixture construction, preflight comparisons, and full content checks
are outside timing. `--self-test` checks all paths using small fixtures without
running throughput measurements. `--direction` selects decode, encode,
validation, or all rows; `--transport` selects full, 1460, 4096, 65536, or all.
Indefinite strings are decode/validate workloads: their CBOR chunks are assembled
outside timing through the ordinary encoder. `--cbor-chunk` defaults to 256;
selftests also check one-byte and 17/4096-byte CBOR chunks against every
transport mode. PMR allocation rows count only destination requests and their
total requested bytes, excluding source descriptors and coroutine frames.
Reserved allocation counts exclude the initial reserve. Owning typed arrays
and nested structured allocations are not PMR-instrumented and report
`allocation-unavailable`. No timing thresholds are used in selftests.
PMR counters start after destination construction in both modes, so constructor
allocations are excluded even in cold counts; cold timing includes construction.

For an ordinary-code baseline, compile the same harness against the baseline
checkout's headers with `CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY=1` (or the matching
CMake option). This excludes resumable operation instantiations and retains the
same ordinary rows and fixtures; the typed-array codec still includes its
shared resumable declarations. Compare `--mode one-shot` from both builds with the
same compiler and flags, alternating whole executable runs. Run comparisons
sequentially on otherwise idle hardware; a current ordinary/resumable comparison
alone does not establish whether ordinary decoding regressed from the baseline.

`bench_resumable_latency` measures real elapsed completion time while an
independent producer admits chunks on absolute `steady_clock` deadlines. It
compares buffering until the final arrival followed by ordinary decode with
incremental resumable decode. This is an in-process arrival benchmark over
prebuilt input, without network transfer or ingress-copy costs. The producer
continues admitting bytes independently when the decoder falls behind; the
consumer may then supply several already-arrived chunks together.

```bash
# Full requested experiment: about 16 minutes plus fixture construction/decode.
build/resumable-bench/benchmarks/resumable/bench_resumable_latency

# Shorter manual experiment with the same scheduling and correctness checks.
build/resumable-bench/benchmarks/resumable/bench_resumable_latency \
  --bytes 1000000 --rate 100000000 --chunk 4096 --repetitions 3
```

Defaults are **exactly 1,000,000,000 encoded bytes**, **100,000,000 bits/s**,
65536-byte chunks, and three paired repetitions for each of two workloads:
nested records and one large bstr. Thus each trial has a nominal 80-second
arrival interval; both modes across both workloads make 12 trials. Structured
fixtures use a small padding bstr to hit the exact encoded length. The source
value, encoded bytes, and destination coexist, so a full run needs several GB
of memory. Both modes use fresh, unreserved destinations. Full equality and byte
count checks and destination destruction run after the completion timestamp.

The first chunk arrives after its transmission duration. For every sample the
CLI reports total completion seconds, the actual final arrival time, final-byte
to completion tail, and maximum/mean/final scheduling lateness. Summary lines
report median/minimum/maximum and sample count for each metric. Scheduling
lateness remains visible instead of being attributed to decoder latency; three
samples do not establish a p95. `--workload structured|bstr`,
`--mode one-shot|resumable`, `--bytes`, `--rate`, `--chunk`, and `--repetitions`
select smaller or focused experiments. Some requested bstr sizes at CBOR header
width transitions are unrepresentable and are rejected explicitly. Structured
fixtures require at least 8192 encoded bytes.

Only the small `--self-test` cases are registered with CTest. They verify exact
fixture size, decoded content, completion after the last arrival, and that the
publisher never admits bytes before their deadlines. They do not impose timing
thresholds or run the full 1 GB experiment.

### Measured container and typed-array changes

The comparison uses baseline headers `cd3abb6b` and implementation `8e94b97f`,
with the same expanded harness for both. Measurements used an AMD Ryzen 9 7950X,
GCC 16.2.1, GNU C++20, `-O3 -DNDEBUG`, and no LTO. Throughput runs were pinned to
CPU 2 and run sequentially, with three alternating baseline/candidate pairs and
nine epochs per row. The reported governor was `powersave`; frequency scaling
remained enabled. These are local measurements, not CI performance thresholds.

Scalar fixtures used a 16 MiB input budget (15,379,114 encoded bytes for the
definite array). Maps, typed arrays, and indefinite strings used a 1 MiB budget;
their actual encoded sizes were approximately 0.55, 1.40, and 1.05–1.11 MB.
These repeatedly reused fixtures can fit in cache. Encoded MB/s is decimal;
large typed-array and string rates should not be extrapolated to sustained
1 GB memory or network throughput.

Cold resumable decoding with **65,536-byte transport chunks** measured the
following. Entries are median [minimum, maximum] across three executable runs.
Speedups are medians [minimum, maximum] of each candidate/baseline pair, not the
ratio of the two displayed medians. An indefinite string's suffix is its CBOR
child payload size, independently of the transport chunk size.

| Workload | Baseline MB/s | Current MB/s | Paired speedup |
|---|---:|---:|---:|
| scalars | 1,820.1 [1,811.8, 1,821.2] | 1,979.0 [1,972.1, 1,981.2] | 1.088 [1.087, 1.089]x |
| scalars-indef | 98.9 [98.8, 100.2] | 804.0 [802.0, 808.6] | 8.130 [8.000, 8.186]x |
| map | 86.2 [84.9, 87.9] | 119.4 [113.3, 120.5] | 1.371 [1.315, 1.405]x |
| map-indef | 62.8 [61.8, 63.3] | 123.2 [121.9, 124.0] | 1.963 [1.958, 1.973]x |
| unordered-map | 142.3 [141.3, 142.9] | 268.8 [258.3, 270.0] | 1.898 [1.807, 1.902]x |
| unordered-map-indef | 82.1 [65.5, 83.9] | 239.4 [234.8, 239.9] | 2.921 [2.853, 3.587]x |
| typed-native | 511.9 [472.4, 516.9] | 40,832.1 [40,472.6, 41,281.3] | 79.773 [78.295, 87.393]x |
| typed-opposite | 504.3 [501.3, 506.4] | 3,431.4 [3,411.4, 3,434.6] | 6.805 [6.783, 6.805]x |
| bstr-indef-16 | 386.9 [384.5, 388.5] | 424.7 [423.5, 427.9] | 1.098 [1.090, 1.113]x |
| bstr-indef-65536 | 30,407.8 [30,301.8, 30,413.0] | 32,147.6 [32,003.0, 32,280.8] | 1.057 [1.056, 1.061]x |
| tstr-indef-16 | 368.2 [325.2, 369.9] | 417.0 [378.1, 420.8] | 1.143 [1.022, 1.282]x |
| tstr-indef-65536 | 23,543.9 [23,495.4, 23,746.8] | 31,800.0 [31,617.8, 32,424.9] | 1.351 [1.346, 1.365]x |

Current cold decode medians at the other tested transport sizes were:

| Workload | Full buffer, MB/s | 1,460 bytes, MB/s | 4,096 bytes, MB/s |
|---|---:|---:|---:|
| scalars | 2,032.4 | 1,096.6 | 1,539.7 |
| scalars-indef | 813.1 | 562.5 | 696.7 |
| map | 94.9 | 134.5 | 139.9 |
| map-indef | 97.7 | 135.9 | 143.3 |
| unordered-map | 270.9 | 240.8 | 260.0 |
| unordered-map-indef | 240.1 | 208.5 | 224.3 |
| typed-native | 42,813.3 | 17,142.3 | 30,506.3 |
| typed-opposite | 3,434.8 | 3,076.6 | 3,272.1 |
| bstr-indef-16 | 428.1 | 419.0 | 424.7 |
| bstr-indef-65536 | 32,670.7 | 16,789.7 | 24,704.4 |
| tstr-indef-16 | 425.1 | 412.9 | 418.1 |
| tstr-indef-65536 | 32,240.2 | 17,015.9 | 24,512.3 |

Full-buffer definite scalar decode improved from 1,851.8 [1,851.2, 1,861.0] to
2,032.4 [1,995.4, 2,034.0] MB/s. Long-child strings were nearly unchanged with a
full buffer: paired speedups were 1.016x [1.009, 1.027] for bstr and 0.995x
[0.985, 1.001] for tstr. Tiny text chunks showed more run-to-run variation than
most rows; the complete ranges above retain it. Chunk-size effects are not
uniform across these workloads.

Capacity affects the indefinite integer case substantially: current
`decode-reserved` reached 1,605.3 [1,604.2, 1,608.7] MB/s at 64 KiB, versus
804.0 [802.0, 808.6] cold, with a paired baseline improvement of 15.218x
[14.851, 15.275]. Definite arrays already reserve their declared count, so their
cold and reserved medians were close (1,979.0 and 1,972.5 MB/s). Typed reserved
rows have the offered-capacity caveat described above; they are not equivalent
allocation controls.

At 64 KiB, map encoding improved by 5.720–5.880x across the four map/wire-form
combinations. Native and opposite-endian typed encoding improved by 11.029x
[10.725, 11.350] and 25.995x [25.314, 26.093], respectively. The baseline typed
codec uses the ordinary fallback; the current owning decode and encode hooks
retain progress across fragments. Native-endian payload copies also benefit
from this small, repeatedly reused fixture.

Not every row improved. Definite scalar resumable encode fell from 3,386.0
[3,385.9, 3,388.9] to 3,180.9 [3,135.9, 3,192.0] MB/s at 64 KiB: paired 0.939x
[0.926, 0.942]. Its resumable validation fell from 206.5 [206.4, 209.3] to
178.7 [175.6, 179.1] MB/s: paired 0.854x [0.851, 0.867]. These rows remain
reported rather than being treated as evidence of universal improvement.

### Ordinary-only controls and allocation counts

Both control executables defined `CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY=1`; this
avoids comparing an ordinary-only baseline with a current translation unit
that additionally instantiates resumable operations. The same three-pair
protocol measured the following ordinary decode results:

| Workload | Baseline MB/s | Current MB/s | Paired speedup |
|---|---:|---:|---:|
| scalars | 1,014.6 [1,012.6, 1,015.7] | 1,015.9 [1,015.7, 1,050.2] | 1.001 [1.000, 1.037]x |
| structured | 2,022.7 [2,015.9, 2,081.3] | 2,166.1 [2,068.7, 2,188.8] | 1.052 [1.023, 1.075]x |
| bstr | 39,988.4 [39,003.6, 40,416.9] | 39,997.6 [39,161.1, 41,013.9] | 1.015 [0.979, 1.025]x |
| tstr | 40,514.6 [39,845.8, 40,985.7] | 39,518.3 [38,177.5, 40,377.5] | 0.964 [0.958, 0.997]x |
| map | 87.4 [86.7, 87.4] | 89.0 [88.7, 90.0] | 1.027 [1.014, 1.030]x |
| map-indef | 88.0 [87.1, 88.2] | 92.3 [91.2, 92.6] | 1.049 [1.034, 1.063]x |
| unordered-map | 261.1 [260.3, 267.2] | 265.0 [263.9, 265.7] | 1.014 [0.992, 1.017]x |
| unordered-map-indef | 205.9 [196.3, 209.4] | 228.6 [221.9, 238.2] | 1.130 [1.110, 1.137]x |
| scalars, reserved | 1,086.2 [1,083.4, 1,088.6] | 1,013.4 [976.7, 1,045.7] | 0.933 [0.897, 0.965]x |
| tstr, reserved | 40,083.6 [39,766.1, 40,085.2] | 38,901.0 [37,173.4, 39,347.4] | 0.978 [0.927, 0.982]x |

Thus the measurements do not establish zero ordinary-code regressions:
reserved scalar decoding was lower in all three pairs, and ordinary tstr
cold decoding was also lower. Ordinary scalar encode was 1.009x
[0.977, 1.138]; ordered definite-map encode was 0.989x [0.979, 0.994], while
hash-map encode improved to 1.792x [1.781, 1.800]. Reader inline hints keep the
scalar bulk loop inline. This full benchmark executable's `.text` was 596,569
bytes versus the baseline's 552,793 bytes (+7.9%). This is a whole-harness
code-size observation, not a per-application guarantee.

Separate PMR probes verified destination equality and the allocation effect of
reserving from each definite child header inside an indefinite string. For one
64 KiB CBOR child split into 1,460-byte transport chunks, bstr allocations fell
from 8 to 1 and tstr from 7 to 1. For one 1 MiB child they fell from 12/11 to
1/1. A 1 MiB payload made of 64 KiB children needed 5 allocations in either
container after the change, independent of tested transport size. One-byte
CBOR children retained the same geometric growth counts: 21 bstr and 17 tstr
allocations for a 1 MiB payload, with unchanged total requested bytes.

To reproduce, build this harness against each header revision with identical
flags. For each of three pairs, run one executable then the other, reversing
the order in pair two:

```bash
taskset -c 2 <executable> --workload scalars --bytes 16777216 \
  --storage both --repetitions 1 --epochs 9
taskset -c 2 <executable> --workload map-indef --bytes 1048576 \
  --storage both --repetitions 1 --epochs 9
taskset -c 2 <executable> --workload tstr-indef --cbor-chunk 65536 \
  --bytes 1048576 --storage both --repetitions 1 --epochs 9
```

Repeat for the table's workloads and for both CBOR string child sizes. For the
macro-matched ordinary controls add `--mode one-shot`; use the 16 MiB budget
for scalar/structured/bstr/tstr and 1 MiB for maps. All fixture selftests,
allocation selftests, and timed preflight/content checks passed. Validation
throughput is encoded-byte accounting: a definite bstr validator may skip its
admitted payload, so its rate is not a measurement of reading every payload byte.

### Paced completion at 100 Mb/s

The final implementation was also measured with **exactly 1,000,000,000 encoded
bytes at 100,000,000 bits/s**, 65,536-byte transport chunks, and three alternating
ordinary/resumable pairs for each workload. Both modes used the current native
latency executable. Producer and consumer CPU affinity was unrestricted; all
other benchmark/build jobs were stopped. All twelve trials passed exact byte
count, full content, and arrival-order checks. Entries summarize all three
samples per mode as median [minimum, maximum].

| Workload | Ordinary completion, s | Resumable completion, s | Ordinary final-byte tail, ms | Resumable final-byte tail, µs |
|---|---:|---:|---:|---:|
| structured | 80.100828 [80.099227, 80.101610] | 80.000097 [80.000082, 80.000100] | 100.766 [99.173, 101.542] | 44.121 [20.940, 44.881] |
| bstr | 80.369200 [80.368666, 80.369721] | 80.000084 [80.000079, 80.000107] | 369.145 [368.598, 369.668] | 28.661 [24.230, 39.141] |

Actual final arrivals ranged from 80.000052272 to 80.000068288 seconds.
Publisher scheduling measurements were:

| Workload/mode | Final lateness, µs | Mean lateness, µs | Maximum lateness, ms |
|---|---:|---:|---:|
| structured, one-shot | 62.871 [53.709, 68.044] | 62.548 [60.563, 63.257] | 0.457 [0.428, 0.477] |
| structured, resumable | 56.083 [52.272, 61.402] | 61.066 [60.864, 65.082] | 0.455 [0.453, 0.458] |
| bstr, one-shot | 54.395 [53.344, 68.074] | 63.262 [61.271, 65.770] | 0.446 [0.420, 0.478] |
| bstr, resumable | 55.772 [54.650, 68.288] | 62.753 [61.783, 63.334] | 0.454 [0.453, 0.454] |

The nominal 80-second arrival interval dominates total completion: comparing
mode medians, completion was 0.126% sooner for structured input and 0.459% sooner
for bstr. The shorter final-byte tail reflects work completed during arrival.
These are scheduled arrivals over prebuilt in-process input, without actual
network transfer or ingress-copy costs; three samples do not establish tail
percentiles. To reproduce, run the default native latency command above on
otherwise idle hardware.
