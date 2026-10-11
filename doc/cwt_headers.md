# Experimental typed COSE headers

`basic_header_map<Borrowed, Extra>` adds application-selected fields to the common algorithm, key ID, critical-label list, and type fields. Define `cwt_fields(Extra&)` and `cwt_fields(const Extra&)` using the [field bindings](cwt_fields.md). Extra fields cannot redefine the common labels.

Decoded headers retain all received labels for duplicate checking, including labels whose values the application discards. Re-encoding emits only selected values. COSE messages retain the original protected byte string, which is the input used for signature verification.

[Example 31](../examples/31_experimental_cwt_type.cpp) introduces RFC 9596 `typ`: an unsigned content-format number or text media type describing the complete COSE object. It belongs in protected headers. Parsing a type does not authenticate it; the receiving application checks its expected type after signature verification.

[Example 32](../examples/32_experimental_cwt_header_claims.cpp) binds a custom claims map under header 15, signs it with optional OpenSSL, verifies it, and compares its issuer with the payload issuer. This implements the RFC 9597 requirement to compare overlapping claims semantically unless an application profile explicitly specifies different processing.

## Authenticated parsing

Pass a typed destination and critical-parameter processor to `verify_sign1<Backend, ExtraCodecs...>(key, message, authenticated, process, aad, detached)`. The verifier parses the message's original protected bytes with that codec pack, checks header placement and duplicates, verifies the signature, and invokes `process(scope, label, parsed_headers)` for each critical extension. It assigns the destination only after all steps succeed.

The processor returns `expected<void, status_code>`. Unknown or unsupported critical extensions must fail. Merely decoding a field does not process its semantics. COSE_Sign verification accepts separate body and signer destinations and identifies their scope to the processor.

Borrowed destinations refer to the message's protected bytes; retain the message and its backing storage. Optional external AAD and detached payload bytes are supplied by the application and must match what the signer used. Critical processing and ordinary application policy remain separate: the application still checks the expected type, issuer, audience, expiry, and any profile-specific relationships it requires.

Keep parsed output objects separate from objects that own the input bytes. Caller-owned signature output must not overlap signed input; input storage stays stable throughout the operation.
