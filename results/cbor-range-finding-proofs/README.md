# CBOR range/lazy-tag review proof tests

These standalone tests prove review findings against commit `cc72f8b`.

Run:

```bash
results/cbor-range-finding-proofs/run-proofs.sh
```

The suite includes:

- runtime proof that `as_map_range` accepts a 3-tuple entry and silently drops the third field
- compile-fail proof that rvalue/owning range wrappers are rejected by the encoder call path
- compile-fail proof that `ValidCborBuffer` accepts byte subranges that `make_encoder` cannot instantiate
- runtime proof that lazy tag scanning allocates and lets `std::bad_alloc` escape
- AddressSanitizer proof that `find_tags` can dangle when called with a temporary buffer
- type proof that `make_decoder().decode(...)` uses raw `status_code` semantics, unlike the expected-return call wrapper
