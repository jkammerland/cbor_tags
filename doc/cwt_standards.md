# CWT and COSE standards coverage

A wire model, a cryptographic provider, and an application protocol are separate
capabilities. Recognizing an identifier does not implement its algorithm.

## Foundations and evolution

| Specification | Status and library boundary |
|---|---|
| [RFC 7049](https://www.rfc-editor.org/info/rfc7049) | Obsoleted by RFC 8949. |
| [RFC 8949](https://www.rfc-editor.org/rfc/rfc8949.html) | Current CBOR foundation; see [CBOR conformance](conformance.md). |
| [RFC 8152](https://www.rfc-editor.org/info/rfc8152) | Obsoleted by RFC 9052 and RFC 9053. Explicit legacy countersignature verification is planned. |
| [RFC 8392](https://www.rfc-editor.org/rfc/rfc8392.html) | Current CWT foundation. Base claims and signed tokens; MAC/encrypted/nested protection is not implemented by the CWT helpers. |
| [RFC 9052](https://www.rfc-editor.org/rfc/rfc9052.html) | COSE structures, updated by RFC 9338. Sign1 and Sign are implemented; required services and algorithms depend on the application profile. |
| [RFC 9053](https://www.rfc-editor.org/rfc/rfc9053.html) | Initial algorithms, updated by RFC 9864. Optional OpenSSL P-256/SHA-256 provider. |
| [RFC 9864](https://www.rfc-editor.org/rfc/rfc9864.html) | Fully specified algorithm identifiers. ESP256 is the new-generation P-256/SHA-256 identifier; legacy ES256 remains a distinct verification format. |

## Feature evidence

This table lists implemented capabilities and their dedicated regression suites.

| Capability | Evidence | Boundary |
|---|---|---|
| Base claims, Sign1 and Sign models | `test_cwt_roundtrip.cpp`, `test_cwt_wire.cpp` | Owning and borrowed models; unknown claims are skipped. |
| Segmented signing and verification | `test_cwt_signing_roundtrip.cpp`, `test_cwt_signing_wire.cpp` | Original protected bytes and payloads are used; caller constructs external AAD. |
| Optional real crypto | `test_cwt_openssl_roundtrip.cpp`, `test_cwt_openssl_wire.cpp` | OpenSSL is optional; custom providers use the same segment contract. |
| Caller storage and custom layouts | Examples 27–29 and CWT signing tests | Borrowed collections may allocate descriptors; source bytes remain caller-owned. |
| Typed map/header extensions | `test_cwt_fields_*`, `test_cwt_headers_*`, examples 30–32 | Selected fields only; automatic preservation of unknown values is outside scope. |
| Countersignatures | Planned follow-up stack | V2 full/abbreviated operations and explicit legacy full verification. |
| Public COSE keys and confirmation | Planned follow-up stack | EC2, OKP, RSA public forms; confirmation and deterministic thumbprints. |

## Direct extensions

| Specification | Follow-up |
|---|---|
| [RFC 8747](https://www.rfc-editor.org/rfc/rfc8747.html) | Public confirmation methods planned. Proof of presenter possession remains an application operation. |
| [RFC 9338](https://www.rfc-editor.org/rfc/rfc9338.html) | Countersignature stack planned for Sign1, individual signatures, and full countersignatures; other targets deferred. |
| [RFC 9360](https://www.rfc-editor.org/rfc/rfc9360.html) | X.509 header representations and trust/path validation need separate work. |
| [RFC 9596](https://www.rfc-editor.org/rfc/rfc9596.html) | Protected `typ` header implemented. Application selects expected media type. |
| [RFC 9597](https://www.rfc-editor.org/rfc/rfc9597.html) | CWT Claims header implemented. Replicated claims require application semantic comparison or explicit profile rules. |
| [RFC 9679](https://www.rfc-editor.org/rfc/rfc9679.html) | Deterministic public-key thumbprints planned with optional hash providers. |
| [RFC 9781](https://www.rfc-editor.org/rfc/rfc9781.html) | UCCS is a distinct externally protected format, not an unsigned tag-61 token. Dedicated profile deferred. |
| [RFC 9921](https://www.rfc-editor.org/rfc/rfc9921.html) | Timestamp headers and RFC 3161 verification deferred. |
| [RFC 9942](https://www.rfc-editor.org/rfc/rfc9942.html) | COSE receipts and proof verification deferred. |
| [RFC 9995](https://www.rfc-editor.org/rfc/rfc9995.html) | Hash-envelope semantics deferred; these differ from segmented signing of a complete payload. |

## Algorithms and application profiles

Header algorithm identifiers currently use integers; text-valued algorithms remain unsupported.

Additional providers are independent work: RSA (RFC 8230), HSS/LMS (RFC 8778),
WebAuthn algorithms (RFC 8812), WalnutDSA (RFC 9021), hashes (RFC 9054),
AES-CTR/CBC (RFC 9459), KangarooTwelve/TurboSHAKE (RFC 9861), and ML-DSA
(RFC 9964). Registration does not establish that an algorithm is recommended.
Consult the current [COSE registry](https://www.iana.org/assignments/cose/).
Pure EdDSA and ML-DSA providers must preserve their specified message semantics;
a provider needing contiguous scratch storage must make that requirement explicit.

Application branches are inventoried separately from core CWT support:

- OAuth/ACE: RFC 8693, 9200, 9201, 9202 (updated by 9430), 9203, 9237,
  9431, 9594, and 9770.
- OSCORE/EDHOC: RFC 8613, 9528, 9529, and 9668. RFC 9528 also registers
  key-bearing CWT/claims headers.
- Attestation: RFC 9334, 9711, 9782, 9783, 9999, and 10013.
- SCITT: RFC 9943 builds on signed statements and receipts.

These protocols are not implicitly implemented by accepting their CBOR labels.
For registered claims, consult the [CWT registry](https://www.iana.org/assignments/cwt/).

SD-CWT, OAuth status lists, CBOR-encoded certificates,
and COSE HPKE are Internet-Drafts, not published RFC capabilities of this library.
Recheck their publication status before beginning a provider or profile implementation.
Reported errata are not treated as adopted normative changes.
