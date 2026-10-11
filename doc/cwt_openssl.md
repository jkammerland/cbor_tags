# OpenSSL ESP256 backend

**Experimental:** The optional OpenSSL ESP256 backend API may change.

Include `cbor_tags/cwt/openssl_crypto.h` for the optional OpenSSL implementation.
See [signing and verification](cose_signing.md) for backend-independent policy.

```cpp
#include <cbor_tags/cwt/openssl_crypto.h>

using namespace cbor::tags;

// key: EVP_PKEY* containing an EC P-256 key; bytes: encoded claims above.
if (bytes) {
    auto message = cwt::sign1<cwt::openssl_esp256_backend>(key, {}, {}, *bytes);
    if (message) {
        auto verified = cwt::verify_sign1<cwt::openssl_esp256_backend>(key, *message);
        if (verified) {
            std::vector<std::byte> token;
            auto result = make_encoder<codec::cwt>(token)(
                cwt::as_cwt(cwt::as_cose_sign1(*message)));
            // Check result before sending token.
        }
    }
}
```

`as_cwt`, `as_cose_sign1` and `as_cose_sign` add tags 61, 18 and 98.
[COSE_Sign1 (RFC 9052 §4.2)](https://www.rfc-editor.org/rfc/rfc9052.html#section-4.2)
contains one signature; [COSE_Sign (§4.1)](https://www.rfc-editor.org/rfc/rfc9052.html#section-4.1)
contains one or more:

```cpp
auto message = cwt::sign<cwt::openssl_esp256_backend>(key, {}, {}, *bytes);
if (message) {
    auto result = cwt::verify_sign<cwt::openssl_esp256_backend>(key, *message);
}
```

Check `bytes` first. Use `add_signature` or `sign_signature` for more signatures.
`make_sign1_tbs` and `make_sign_tbs` produce the corresponding
[Sig_structure (§4.4)](https://www.rfc-editor.org/rfc/rfc9052.html#section-4.4).

The built-in crypto backend supports **ESP256 only**: P-256, SHA-256 and COSE's
64-byte `r || s` signature. `algorithm::esp384`, `algorithm::esp512`, `algorithm::ed25519`, and `algorithm::ed448` are wire
identifiers, not built-in crypto implementations. Other algorithms/providers can use the custom backend contract in the signing guide.


## Build and package

Enable `-DCBOR_TAGS_ENABLE_CWT_OPENSSL=ON` and link `cbor::cwt_openssl` for
OpenSSL signing. Conan uses `-o cbor-tags/*:cwt_openssl=True`; vcpkg uses
`--x-feature=cwt-openssl`. Conan's aggregate target is `cbor::all`.

`-DCBOR_TAGS_STL_ONLY=ON` selects C++26 standard-library facilities without
public third-party dependencies. OpenSSL remains an explicit dependency when
its crypto backend is enabled.

New signatures use the fully specified ESP256 identifier (-9) from [RFC 9864](https://www.rfc-editor.org/rfc/rfc9864.html). For existing ES256 (-7) tokens, explicitly select `openssl_es256_verifier`; it provides verification only. Neither verifier retries another algorithm or rewrites protected bytes. The legacy verifier accepts P-256 keys only, matching this library's earlier provider.
