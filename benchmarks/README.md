# Benchmarks

These benchmarks are intended to track codec behavior over time. Keep existing
benchmark bodies stable unless a benchmark is clearly measuring setup work
instead of the path named in its row.

## Decoder `CHECK(...)` Rows

Rows such as `Decoding a uint with check` use doctest assertions:

```cpp
CHECK(dec(value));
```

Those rows measure doctest assertion machinery plus `tl::expected` truth
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
