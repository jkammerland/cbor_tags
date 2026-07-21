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

## Claims Validation

`claims_set` accepts both definite-length and break-terminated indefinite-length
CBOR maps. Every decoded integer or text claim label must be unique, including
unknown and non-canonically encoded labels. A duplicate is rejected instead of
applying first-value-wins or last-value-wins semantics because conflicting claim
values are not interoperable and are unsafe inputs for authorization decisions.
This matches RFC 8949's treatment of duplicate map keys as invalid CBOR.

Unknown integer and text claims are consumed but not retained. Integer
`NumericDate` values must fit in `std::int64_t`; values outside that range are
rejected rather than narrowed. Failed claim-map validation leaves the
destination `claims_set` unchanged.

See [RFC 8392](https://www.rfc-editor.org/rfc/rfc8392.html) for CWT claims and
[RFC 8949 Section 5.3.1](https://www.rfc-editor.org/rfc/rfc8949.html#section-5.3.1)
for duplicate-map-key validity.

`claims_set::audience` is `std::optional<cwt::audience_claim>`, where
`cwt::audience_claim` is `std::variant<std::string, std::vector<std::string>>`.
This matches CWT's compact single-audience string form and its general
array-valued audience form. Unknown claim keys, including private text-string
keys, are consumed during decode but are not retained.

## Header Validation

`header_map` models the registered `alg` and `kid` parameters and the protected
`crit` parameter. Header labels use `header_label`, which accepts either a CBOR
integer or text string. Unknown noncritical labels are consumed during decode
but are not retained.

Critical labels must be unique, must identify a parameter in the same protected
map, and must be one of the parameters implemented by this API: integer label
`1` (`alg`) or `4` (`kid`). An empty critical-label array, unknown critical
label, duplicate map label, or trailing item in a protected-header byte string
is rejected. Signing and verification also reject `crit` in unprotected
headers.

Duplicates are rejected within each protected or unprotected map. The current
`header_map` model does not perform a general duplicate-label check across the
two buckets. It discards unknown noncritical parameters, so it cannot compare
their labels after decoding; it also leaves supported parameters such as `kid`
in their respective protected and unprotected objects. This is a permissive
policy relative to the recommendation in
[RFC 9052 Section 3](https://www.rfc-editor.org/rfc/rfc9052.html#section-3)
that applications check for the same label in both buckets.

Applications that require that recommendation as a strict profile must reject
cross-bucket duplicates before converting unknown parameters to `header_map`.
Known `kid` duplication can be checked after decoding:

```cpp
auto protected_map = cwt::decode_protected_header(message.protected_header);
if (!protected_map || (protected_map->kid && message.unprotected.kid)) {
    return reject_message();
}
```

The signing and verification helpers remain stricter for security-sensitive
registered parameters: `alg` and `crit` are rejected in unprotected headers.

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

Package managers keep the crypto backends opt-in as well:

```bash
conan install . -o cbor-tags/*:cwt_openssl=True
vcpkg install --x-feature=cwt-openssl
```

Conan's generated aggregate target is `cbor::all`. Link the specific component
target (`cbor::tags`, `cbor::cwt`, or `cbor::cwt_openssl`) when dependency
propagation matters.

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
`COSE_Sign1`. The OpenSSL ES256 backend signs and verifies those bytes and
stores COSE's raw 64-byte `r || s` ECDSA signature representation.
The backend requires an EC key on the P-256 (`prime256v1`) curve; other EC
curves and non-EC keys are rejected before signing or verification.
The `sign1(...)` helper writes `alg` to the protected header when it is not
already set, rejects an algorithm that conflicts with the selected backend, and
rejects `alg` in the unprotected header.

For a message whose payload field is `null`, detached-payload arguments use
`std::optional<std::span<const std::byte>>`. `std::nullopt` means that no
payload was supplied and is an error. An engaged empty span is a valid detached
empty payload:

```cpp
auto result = cwt::verify_sign1<cwt::openssl_es256_backend>(
    key,
    message,
    {},
    std::span<const std::byte>{});
```

When the message contains an embedded payload, it takes precedence over any
detached payload argument.

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

## Custom Backend Example

Crypto providers can be integrated outside this package by defining a backend
type with the same static API used by the built-in OpenSSL backend:

```cpp
namespace tags = cbor::tags;
namespace cwt = cbor::tags::cwt;

struct custom_es256_backend {
    static constexpr cwt::algorithm algorithm_id = cwt::algorithm::es256;

    static tags::expected<cwt::byte_string, tags::status_code>
    sign(void *key, std::span<const std::byte> to_be_signed);

    static tags::expected<void, tags::status_code>
    verify(void *key,
           std::span<const std::byte> to_be_signed,
           std::span<const std::byte> signature);
};

auto message = cwt::sign1<custom_es256_backend>(key, {}, {}, payload);
```
