# Experimental public keys and confirmation

These APIs represent public COSE keys; they do not imply a crypto provider for every represented algorithm. Include `<cbor_tags/codec/cwt.h>` and opt in with `codec::cwt`.

`ec2_public_key`, `okp_public_key`, and `rsa_public_key` own key material. Their `_view` counterparts borrow byte/text fields, while metadata collections may allocate. `as_view(owner)` requires a living lvalue owner. EC2 coordinates preserve fixed-width padding; RSA unsigned integers reject redundant zero padding. Decoding requires the concrete key type and public parameters, rejects private parameters and duplicate labels, and skips unknown fields. Structural decoding does not establish mathematical validity.

`confirmation<Key>` and `confirmation_view<Key>` represent a selected `cnf` layout. Bind them to claim 8 using `fields(field<8>(confirmation))`. Public key, key identifier, and SHA-256 thumbprint may coexist. Unknown confirmation methods are ignored; inline and encrypted key methods together are rejected even though encrypted-key processing is not implemented. A recipient must resolve identifiers and confirm that all supplied methods describe the same key. Parsed confirmation metadata is neither authenticated claims nor proof of possession.

`make_key_thumbprint_input(key)` returns ordered, owned CBOR headers and views into the original public key data. It selects only required public parameters and excludes `kid`, `alg`, and `key_ops`, following [RFC 9679](https://www.rfc-editor.org/rfc/rfc9679.html). The returned segments must not outlive the key. Text curve identifiers require application resolution; compressed EC2 points require explicit expansion.

`key_thumbprint<SHA256>(key)` returns a 32-byte array; an overload writes to `span<byte, 32>`. A provider supplies `digest(parts, output) -> expected<void, status_code>` and consumes the input range of byte spans exactly once. Provider errors propagate. A custom provider can process segments without assembling the whole CBOR map.

Including `<cbor_tags/cwt/openssl_sha256.h>` exposes the optional `openssl_sha256` hash provider. `<cbor_tags/cwt/openssl_keys.h>` additionally exposes `expand_p256_public_key` and `import_p256_public_key`. Link the optional `cbor::cwt_openssl` target. Expansion validates a P-256 point and produces an owning uncompressed model. Import validates the point, expected algorithm, and verification permission; the resulting RAII handle retains the admitted algorithm and is accepted by the corresponding OpenSSL verifier. Its explicit `get()` is an escape hatch to the crypto library, where the application assumes responsibility for metadata policy. New examples use ESP256; legacy ES256 requires explicit selection.

Custom providers should call `validate_key_use(key, algorithm, operation)` before converting to their native handles. Absent `key_ops` imposes no operation restriction; an explicitly empty collection permits no operation. This check does not perform cryptographic validation or key resolution.

Examples 36–39 introduce representation, confirmation, thumbprints, then a complete signed confirmation token. MAC/encryption, secret keys, automatic key discovery, and proof-of-possession protocols remain outside these helpers.
