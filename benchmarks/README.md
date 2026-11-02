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

Compare nanoseconds per encoded byte with the same compiler, optimization flags,
CPU affinity, fixture contents, and epoch settings. Record frequency scaling and
governor settings alongside results; interleave baseline and candidate runs to
make machine drift visible. These rows measure the cost of the structural pass,
not a release-wide performance guarantee.

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

For paired comparisons, build the same harness against both header revisions
with identical flags. Run three baseline/candidate pairs sequentially, reversing
executable order in the second pair. For example:

```bash
taskset -c 2 <executable> --workload scalars --bytes 16777216 \
  --storage both --repetitions 1 --epochs 9
taskset -c 2 <executable> --workload map-indef --bytes 1048576 \
  --storage both --repetitions 1 --epochs 9
taskset -c 2 <executable> --workload tstr-indef --cbor-chunk 65536 \
  --bytes 1048576 --storage both --repetitions 1 --epochs 9
```

Repeat for the relevant workloads and CBOR child sizes. Add `--mode one-shot`
for the ordinary-only controls. Record compiler and build flags, CPU affinity,
frequency scaling, governor, and actual encoded sizes. Report each mode's
median and range, and compute speedup for each pair before summarizing those
ratios; a ratio of independently computed medians is a different statistic.

Encoded MB/s uses decimal megabytes. Repeated fixtures may fit in cache, so
these rates do not establish sustained large-buffer or network throughput.
Validation throughput counts encoded bytes: a definite bstr validator may skip
its admitted payload, so its rate does not measure reading every payload byte.
Run fixture and allocation selftests and retain the timed content checks.

`bench_resumable_latency` measures real elapsed completion time while an
independent producer admits chunks on absolute `steady_clock` deadlines. It
compares buffering until the final arrival followed by ordinary decode with
incremental resumable decode. This is an in-process arrival benchmark over
prebuilt input, without network transfer or ingress-copy costs. The producer
continues admitting bytes independently when the decoder falls behind; the
consumer may then supply several already-arrived chunks together.

```bash
# Default experiment: about 16 minutes plus fixture construction/decode.
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
