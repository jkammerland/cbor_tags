# Experimental CWT and COSE extensions

These opt-in APIs cover tokens, signatures, headers, and public keys. Their APIs
may change; OpenSSL is an optional crypto backend.

- CWT [RFC8392](https://www.rfc-editor.org/rfc/rfc8392.html) — [claims and codec guide](cwt.md), [compiled example](../examples/23_experimental_cwt_claims.cpp).
- COSE [RFC9052](https://www.rfc-editor.org/rfc/rfc9052.html) — [Sign/Sign1 models and signing helpers](cose_signing.md).
- COSE ESP256 [RFC9864](https://www.rfc-editor.org/rfc/rfc9864.html) — [optional OpenSSL backend](cwt_openssl.md).
- COSE countersignatures [RFC9338](https://www.rfc-editor.org/rfc/rfc9338.html) — [signing, verification and supported targets](cwt_countersignatures.md).
- COSE header extensions [RFC9596](https://www.rfc-editor.org/rfc/rfc9596.html) / [RFC9597](https://www.rfc-editor.org/rfc/rfc9597.html) — [typed headers and claims](cwt_headers.md).
- CWT confirmation [RFC8747](https://www.rfc-editor.org/rfc/rfc8747.html) and public-key thumbprints [RFC9679](https://www.rfc-editor.org/rfc/rfc9679.html) — [key models and providers](cwt_keys.md).

See [CWT and COSE standards coverage](cwt_standards.md) for implemented capabilities and follow-ups.
