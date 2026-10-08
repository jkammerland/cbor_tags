# OpenSSL ES256 backend

Include `cbor_tags/cwt/openssl_crypto.h` for the optional OpenSSL implementation.
See [signing and verification](cose_signing.md) for backend-independent policy.

```cpp
#include <cbor_tags/cwt/openssl_crypto.h>

namespace tags = cbor::tags;
namespace cwt = tags::cwt;
namespace codec = tags::codec;

// key: EVP_PKEY* containing an EC P-256 key; bytes: encoded claims above.
if (bytes) {
    auto message = cwt::sign1<cwt::openssl_es256_backend>(key, {}, {}, *bytes);
    if (message) {
        auto verified = cwt::verify_sign1<cwt::openssl_es256_backend>(key, *message);
        if (verified) {
            cwt::byte_string token;
            auto result = tags::make_encoder<codec::cwt>(token)(
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
auto message = cwt::sign<cwt::openssl_es256_backend>(key, {}, {}, *bytes);
if (message) {
    auto result = cwt::verify_sign<cwt::openssl_es256_backend>(key, *message);
}
```

Check `bytes` first. Use `add_signature` or `sign_signature` for more signatures.
`make_sign1_tbs` and `make_sign_tbs` produce the corresponding
[Sig_structure (§4.4)](https://www.rfc-editor.org/rfc/rfc9052.html#section-4.4).

The built-in crypto backend supports **ES256 only**: P-256, SHA-256 and COSE's
64-byte `r || s` signature. `algorithm::es384` and `algorithm::es512` are wire
identifiers, not built-in crypto implementations. Other algorithms/providers can use the custom backend contract in the signing guide.


## Build and package

Enable `-DCBOR_TAGS_ENABLE_CWT_OPENSSL=ON` and link `cbor::cwt_openssl` for
OpenSSL signing. Conan uses `-o cbor-tags/*:cwt_openssl=True`; vcpkg uses
`--x-feature=cwt-openssl`. Conan's aggregate target is `cbor::all`.

`-DCBOR_TAGS_STL_ONLY=ON` selects C++26 standard-library facilities without
public third-party dependencies. OpenSSL remains an explicit dependency when
its crypto backend is enabled.
