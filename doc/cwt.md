# CBOR Web Token And COSE Signing Helpers

`cbor_tags/cwt/cwt.h` provides a crypto-free typed layer for CWT and COSE
signing objects:

- CWT registered claims: `claims_set`
- COSE headers: `header_map`
- COSE signing objects: `cose_sign`, `cose_signature`, and `cose_sign1`
- COSE signing input: `sig_structure`, `make_sign1_tbs(...)`, and
  `make_sign_tbs(...)`
- tag helpers: `as_cwt(...)`, `as_cose_sign1(...)`, and `as_cose_sign(...)`

The base `cbor::tags` target does not link a crypto library.

## Targets

```cmake
target_link_libraries(app PRIVATE cbor::cwt)
```

`cbor::cwt` is header-only and depends only on `cbor::tags`.

Enable OpenSSL support explicitly:

```cmake
-DCBOR_TAGS_ENABLE_CWT_OPENSSL=ON
target_link_libraries(app PRIVATE cbor::cwt_openssl)
```

Enable wolfSSL support explicitly:

```cmake
-DCBOR_TAGS_ENABLE_CWT_WOLFSSL=ON
target_link_libraries(app PRIVATE cbor::cwt_wolfssl)
```

The wolfSSL backend is a separate target so wolfSSL licensing does not propagate
through the base CBOR library target.

Package managers keep the crypto backends opt-in as well:

```bash
conan install . -o cbor-tags/*:cwt_openssl=True
conan install . -o cbor-tags/*:cwt_wolfssl=True
vcpkg install --x-feature=cwt-openssl
vcpkg install --x-feature=cwt-wolfssl
```

The wolfSSL backend needs wolfSSL's OpenSSL-compatible EVP surface. Conan sets
the wolfSSL `opensslextra` and `opensslall` options when
`cwt_wolfssl=True`; non-Conan wolfSSL packages must be built with equivalent
compatibility support.

Conan's generated aggregate target is `cbor::all`. Link the specific component
target (`cbor::tags`, `cbor::cwt`, `cbor::cwt_openssl`, or
`cbor::cwt_wolfssl`) when dependency propagation matters.

## Sign1 Example

```cpp
#include <cbor_tags/cwt/cwt.h>
#include <cbor_tags/cwt/openssl_crypto.h>

namespace cwt = cbor::tags::cwt;

auto claims_bytes = cwt::encode_to_bytes(cwt::claims_set{
    .issuer = "coap://as.example.com",
    .subject = "erikw",
});

auto message = cwt::sign1<cwt::openssl_es256_backend>(
    key,
    cwt::header_map{.kid = cwt::byte_string{std::byte{0x01}}},
    {},
    std::span<const std::byte>{*claims_bytes});

if (message) {
    std::vector<std::byte> token;
    auto enc = cbor::tags::make_encoder(token);
    enc(cwt::as_cwt(cwt::as_cose_sign1(*message)));
}
```

`make_sign1_tbs(...)` builds the COSE `Sig_structure` bytes for
`COSE_Sign1`. The OpenSSL and wolfSSL ES256 backends sign and verify those
bytes and store COSE's raw 64-byte `r || s` ECDSA signature representation.
The `sign1(...)` helper writes `alg` to the protected header when it is not
already set, rejects an algorithm that conflicts with the selected backend, and
rejects `alg` in the unprotected header.

## Sign Example

```cpp
auto message = cwt::sign<cwt::openssl_es256_backend>(
    key,
    {},
    {},
    std::span<const std::byte>{*claims_bytes},
    cwt::header_map{.kid = cwt::byte_string{std::byte{0x01}}});

if (message) {
    auto result = cwt::verify_sign<cwt::openssl_es256_backend>(key, *message);
}
```

`sign(...)` creates a `COSE_Sign` with one `cose_signature`. Additional
signature entries can be created with `sign_signature(...)` or appended with
`add_signature(...)`. For `COSE_Sign`, `alg` may be protected at the body or
signature level, but `alg` in either unprotected header is rejected.
