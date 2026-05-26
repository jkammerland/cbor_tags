# Interop inconsistencies

Known nlohmann/json mismatches captured by these tests:

- CBOR negative integers below `int64_t::min` are silently misdecoded. Observed:
  `-9223372036854775809` becomes `9223372036854775807`.
- Semantic tags are not preserved generally. Default rejects tags, `ignore`
  drops tags, and `store` only maps tagged byte strings to binary subtypes.
- Valid CBOR maps with non-text keys are rejected.
- Duplicate text keys collapse to the last value.
- CBOR simple values outside JSON bool/null, including undefined, are rejected.

Known Glaze mismatches captured by these tests:

- Numeric vectors are emitted as RFC 8746 typed arrays, so plain vector decoding
  in `cbor_tags` needs the typed-array extension for that wire shape.
- Compact half/float encodings are not widened by `cbor_tags` scalar float
  targets.
- Generic semantic tags and simple undefined are rejected by ordinary typed Glaze
  targets.
- Duplicate map keys collapse to the last value when decoding into `std::map`.
