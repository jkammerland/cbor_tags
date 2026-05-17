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
