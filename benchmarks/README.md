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
ctest --test-dir build/resumable-bench -R 'resumable_(throughput|latency)' --output-on-failure
```

`bench_resumable_throughput` compares ordinary encode/decode with resumable
encode/decode supplied as a complete buffer or in 1460, 4096, and 65536 byte
chunks. Separate validation rows compare `validate_item` with the resumable
validator, without creating decoded values. Fixtures are deterministic nested
records, scalar arrays, text, and binary strings. Nested records contain a
4096-character string and 64 integers
spanning CBOR integer widths. The default fixture budget is 16 MiB; scalar
arrays use approximately that many encoded bytes and print the actual count.

```bash
build/resumable-bench/benchmarks/resumable/bench_resumable_throughput
build/resumable-bench/benchmarks/resumable/bench_resumable_throughput \
  --bytes 16777216 --repetitions 3 --epochs 9 --workload structured
```

Each round reverses mode order. Nanobench runs nine epochs of at least 20 ms
per row by default. Output includes every row's encoded MB/s, ns/byte, epoch
minimum/maximum, iteration count, and a median/range across the three rounds.
Decode rows create fresh destinations, including allocation and destruction of
the previous destination. Encode rows reuse equally sized caller-provided
output storage. Operation creation, source descriptors, and chunk feeding are
included. Fixture construction, preflight comparisons, and full content checks
are outside timing. `--self-test` checks all paths using small fixtures without
running throughput measurements.

For an ordinary-code baseline, compile the same harness against the baseline
checkout's headers with `CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY=1` (or the matching
CMake option). This removes the resumable header dependency and retains the same
ordinary rows and fixtures. Compare `--mode one-shot` from both builds with the
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
count checks run after the completion timestamp.

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

### Measurements for the resumable implementation

Measured implementation `fc9c1704` against ordinary baseline `4e3fd952` on an
AMD Ryzen 9 7950X with GCC 16.2.1, GNU C++20, `-O3 -DNDEBUG`. Throughput ran on
one pinned logical CPU. Frequency scaling and the `powersave` governor remained
enabled. The paced producer and consumer used unrestricted CPU affinity.
Subsequent fixes affect variant/tag customizations absent from these fixtures;
the subsequent precision change affects printed output only.

Throughput below is encoded **MB/s**, median **[minimum, maximum]** across three
rounds, each using nine epochs of at least 20 ms. Fixtures contain 16,777,216
encoded bytes, except the scalar array with 15,379,114 bytes. All fixture and
post-measurement content checks passed.

| Workload / operation | Ordinary | Resumable, whole buffer | Resumable, 64 KiB chunks |
|---|---:|---:|---:|
| structured decode | 7658 [7618, 8194] | 3074 [3004, 3129] | 2829 [2343, 2927] |
| structured encode | 15012 [14928, 15554] | 3277 [3256, 3433] | 3329 [3159, 3435] |
| bstr decode | 32221 [31418, 32619] | 32395 [32167, 33009] | 12176 [11208, 12193] |
| bstr encode | 31850 [29825, 32251] | 33176 [32148, 33272] | 32821 [28836, 33169] |
| tstr decode | 29004 [28958, 31346] | 28550 [28517, 32272] | 7471 [7075, 7821] |
| tstr encode | 27711 [27449, 31548] | 28392 [27278, 31649] | 24962 [24677, 25285] |
| scalar decode | 1277 [1270, 1278] | 201 [200, 201] | 196 [195, 197] |
| scalar encode | 3478 [3464, 3492] | 197 [190, 199] | 194 [194, 196] |
| structured validation | 1465 [1411, 1568] | 656 [656, 746] | 738 [662, 739] |
| tstr validation | 1748 [1740, 1756] | 860 [764, 861] | 863 [857, 865] |
| scalar validation | 498 [496, 502] | 207 [179, 208] | 202 [187, 206] |

Binary-string validation skips the declared payload, so its reported represented
bytes/s is not memory-scanning throughput and is omitted from this table.
Resumability has substantial costs for scalar arrays: about 6.5 times longer
decode and 18 times longer encode with 64 KiB chunks. Ready scalar results avoid
per-integer coroutine frames; chunk cursor handling and structural continuation
still cost work. These results do not support replacing every ordinary call with
a resumable call.

The separate baseline comparison compiled **both** harnesses with
`CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY=1`. An earlier comparison using a full candidate
translation unit was discarded because its extra instantiations confounded the
comparison. Three alternating matched pairs gave noisy results: structured
decode was 0.1199 [0.1189, 0.1375] ns/B at baseline and 0.1341 [0.1146, 0.1412]
ns/B in the candidate, while individual paired changes were -2.4%, +17.7%, and
-3.6%. Scalar encode was initially 0.3321 [0.3290, 0.3726] versus
0.3892 [0.3698, 0.4101] ns/B. Binary/text encode and decode medians improved
roughly 3–5% in that run.

A bounded scalar follow-up used the same binaries, three alternating pairs,
and 21 epochs per row. Encode measured 0.3246 [0.3080, 0.3254] versus
0.3336 [0.3292, 0.3345] ns/B, a 2.8% increase in median time; paired increases
were 8.3%, 1.4%, and 2.8%. Decode medians differed by -0.4%. The optimized scalar
encoding wrapper had the same 166 instructions, integer loop, register
allocation, and alignment in both binaries, with a different code address.
No snapshot or coroutine instructions were added to ordinary encoding.
Timing equivalence is nevertheless **not established** by these measurements.

All **12 full paced trials** passed, each admitting exactly 1 GB at 100 Mb/s in
64 KiB chunks. Each table entry is a median [minimum, maximum] across three
trials. Completion includes decoding but excludes the final equality check.

| Workload | Ordinary total, s | Resumable total, s | Ordinary final-byte tail, ms | Resumable final-byte tail, µs |
|---|---:|---:|---:|---:|
| structured | 80.131417 [80.128212, 80.132646] | 80.000109 [80.000104, 80.000147] | 131.350 [128.145, 132.592] | 54.481 [49.411, 84.952] |
| bstr | 80.378311 [80.374206, 80.400302] | 80.000102 [80.000083, 80.000103] | 378.251 [374.151, 400.248] | 46.621 [27.681, 48.161] |

Actual final arrivals were 80.000054–80.000067 seconds after start. The largest
publisher lateness across all trials was 0.536 ms. The roughly 80-second ingress
interval dominates total completion: the median reduction was 0.164% for
structured data and 0.471% for bstr, despite the much shorter final-byte tail.
These are real scheduled arrivals over prebuilt in-process input, not measured
network transport, and three samples do not characterize tail percentiles.
