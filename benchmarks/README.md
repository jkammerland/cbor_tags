# Benchmarks

These benchmarks are intended to track codec behavior over time. Keep existing
benchmark bodies stable unless a benchmark is clearly measuring setup work
instead of the path named in its row.

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

## Shared Graph Encode Lookup Rows

The encoder suite includes `shared_graph encode N unique x2 unordered_map`,
`unordered_map_reserved`, `vector_scan_o_n`, and `vector_scan_o_n_reserved`
rows. They encode the same `std::vector<std::shared_ptr<std::uint64_t>>`: `N`
first-seen pointers followed by a second pass of references to the same
pointers. Reserved rows reuse a pre-reserved `shared_graph_encode_session` and
call `reset()` before each measured encode so table growth is separated from
lookup cost. These rows isolate the encode-side identity lookup tradeoff: hash
lookup is the default for large graphs, while `linear_scan` avoids the hash table
for small or allocation-sensitive graph scopes.

Historical fixed-array lookup experiments were removed from the public API and
benchmark target. The directional numbers below were captured with CPU governor
warnings enabled, so they should be treated as design evidence rather than
release performance data:

| unique | unordered reserved | vector reserved | safe array | typed unsafe array |
|-------:|-------------------:|----------------:|-----------:|-------------------:|
|      4 |          148.08 ns |       100.84 ns |  104.34 ns |           62.69 ns |
|     16 |          606.52 ns |       442.95 ns |  505.33 ns |          287.79 ns |
|     64 |         3326.37 ns |      2665.20 ns | 3038.04 ns |         2596.25 ns |
|    256 |        14721.68 ns |     23152.88 ns | 24786.15 ns |        18851.49 ns |

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
`rfc8746::typed_array_codec` with an owned byte-vector destination. On
little-endian hosts the typed-array payload is appended from the native
contiguous vector bytes, so the hot path is header work plus one payload append.
Rows named `rfc8746 typed array zc ... encode segment assembly (represented bytes)` use
`rfc8746::encode_typed_array_segments(...)`; on little-endian hosts the payload
segment is borrowed from the source vector. Decode rows are split between
`decode owning`, which materializes a `std::vector<T>`, and `decode borrowed`,
which decodes a `typed_array_view<T>` over the input payload without copying the
values. Borrowed decode row names include `view bind (represented bytes)` when
the measured work only parses the header and binds a view over the original
payload.

## Serialization Comparison Suite

The cross-library comparison suite is opt-in because it fetches and builds extra
dependencies. It compares `cbor_tags`, bitsery, zpp_bits, cereal,
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
rows add benchmark-local top-level type detection: CBOR uses a top-level tag,
FlatBuffers uses a file identifier, and binary archives prefix a `uint32_t`
object id. Homogeneous numeric fixtures also include CBOR RFC 8746 typed-array
rows using IANA tags 78, 79, and 86 for little-endian int32, int64, and float64
byte-string payloads. Those rows are intentionally not wire-equivalent to
generic CBOR arrays, and they are benchmark-local helpers rather than a public
typed-array API. The typed-array rows split the existing byte-range encoder,
a contiguous bulk-copy buffer, and a borrowed header-plus-payload segment path
so range overhead and the zero-copy ceiling are visible separately.
