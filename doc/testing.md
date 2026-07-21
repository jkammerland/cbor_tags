# Testing

Tests should separate semantic serialization behavior from CBOR wire-format behavior. This keeps most test data reusable when an encoder or decoder plugin changes the wire protocol.

## Semantic tests

Start from typed C++ values, encode them, decode them, and compare the resulting value with the input. Use `cbor::tags::test::roundtrip` for default-constructible output types and `roundtrip_into` when the destination must be preconfigured.

`roundtrip_into` uses the decoder's ordinary destination semantics. In particular, sequence containers append to existing values; the helper does not clear a preconfigured output unless the decoder for that type normally replaces it.

A feature-focused test should still use a realistic aggregate when the feature composes with reflection. Include relevant enums, engaged and disengaged optionals, every variant alternative, nested containers, and tagged records. Empty and boundary-sized values belong beside the normal case.

For a valid input that should fail only at decode time, prefer encoding a less-constrained typed source value over writing its CBOR representation as hex. Check the status code and any documented destination-state guarantees.

## Wire tests

Use exact bytes or hex when the bytes are the behavior under test, including:

- RFC or interoperability vectors;
- canonical or non-canonical representation choices;
- tag, length, byte-order, or indefinite-form encoding;
- malformed, truncated, or otherwise unrepresentable input.

Keep exact-wire assertions separate from semantic roundtrip assertions. A serializer and decoder can agree on the same wrong representation, while an exact-byte test cannot prove that decoded values survive a refactor.

## Negative tests

Negative coverage should identify the failed contract and expected destination state. Cover wrong major types, invalid tags, truncation at structural boundaries, invalid lengths or indexes, configured limits, and trailing bytes where relevant. Prefer deterministic tables for related malformed cases; fuzzing may supplement but does not replace named regression cases.

## Review checklist

- Does the success path compare decoded values rather than only encoded bytes?
- Are all variant alternatives and both optional states exercised?
- Does at least one larger aggregate cover realistic nesting?
- Are empty, minimum, maximum, and one-past-limit cases covered where limits exist?
- Are CBOR literals limited to wire conformance or malformed-input cases?
- Do extension tests run through the public encoder and decoder factories with the required codec mixins?
