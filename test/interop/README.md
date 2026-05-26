# CBOR interop observations

This directory contains opt-in integration checks against external CBOR stacks.
Enable them with the `debug-interop` preset or with
`CBOR_TAGS_BUILD_INTEROP_TESTS=ON`.

## nlohmann/json

`nlohmann/json` is JSON-shaped first, so several valid CBOR values are not
lossless through it:

- CBOR negative integers below `int64_t::min` are silently misdecoded. In the
  observed nlohmann/json 3.12 behavior, `-9223372036854775809` decodes as
  `9223372036854775807`.
- Semantic tags are not represented generally. The default tag policy rejects
  tags; `ignore` drops the tag and keeps the payload; `store` works for tagged
  byte strings by treating the tag as a binary subtype, but it rejects tagged
  text strings.
- Maps with non-text keys are valid CBOR and decode with `cbor_tags`, but
  nlohmann rejects them because JSON object keys are strings.
- Duplicate text keys collapse to the last value when decoded as a JSON object.
- Simple values other than JSON booleans and null, including CBOR undefined,
  are rejected.

The covered compatible subset includes JSON-shaped maps, arrays, strings,
byte strings, unsigned and signed integer boundaries within nlohmann's range,
finite and non-finite floats, binary subtypes as tagged byte strings, and valid
indefinite JSON-compatible array/map/text/byte-string forms.

## cbor-diag

The Rust `cbor-diag` helper validates generated `cbor_tags` byte vectors by
parsing from hex and bytes, round-tripping back to bytes, converting through
diagnostic notation, and checking malformed vectors are rejected.

Covered vectors include:

- unsigned and signed integer boundaries
- text strings and byte strings
- JSON-shaped maps and nullable arrays
- floats excluding NaN
- CBOR simple values and undefined
- duplicate text keys
- broad and nested tags, including tags `0`, `1`, `23`, `24`, `255`, `256`, and
  `55799`
- maps with non-text keys
- valid indefinite arrays, maps, text strings, byte strings, and nested
  indefinite arrays
- malformed truncation, missing payload/value, and unclosed indefinite forms

NaN is intentionally left out of the `cbor-diag` vector file because equality
and byte-exact diagnostic round-tripping are not stable checks for NaN payloads.
