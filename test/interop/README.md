# Interop inconsistencies

Known nlohmann/json mismatches captured by these tests:

- CBOR negative integers below `int64_t::min` are silently misdecoded. Observed:
  `-9223372036854775809` becomes `9223372036854775807`.
- Semantic tags are not preserved generally. Default rejects tags, `ignore`
  drops tags, and `store` only maps tagged byte strings to binary subtypes.
- Valid CBOR maps with non-text keys are rejected.
- Duplicate text keys collapse to the last value.
- CBOR simple values outside JSON bool/null, including undefined, are rejected.
