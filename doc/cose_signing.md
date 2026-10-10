# COSE signing and verification

**Experimental:** COSE signing and verification APIs may change.

Include `cbor_tags/cwt/cwt.h` for these backend-independent helpers.
See [CWT and COSE models](cwt.md) for codec selection and payload lifetimes.

## Backend contract

The runnable [custom backend example](../examples/24_experimental_cwt_backend.cpp)
uses an application-owned key and OpenSSL as a replaceable provider. Its two
provider calls are the integration points for your crypto library or HSM SDK;
map provider failures to `unexpected<status_code>` and return success only after
signing or verification succeeds. Only this example requires the optional
`CBOR_TAGS_ENABLE_CWT_OPENSSL` build flag.

Keys can be application-defined types; the helpers forward them to your backend.
The backend receives the encoded COSE `Sig_structure`, not just the payload or a
precomputed digest. For ES256, the provider applies SHA-256 and returns a 64-byte
`r || s` signature, converting its native signature format if necessary.

Provide an algorithm identifier and operations with these signatures:

```cpp
#include <cbor_tags/cwt/cwt.h>

namespace tags = cbor::tags;
namespace cwt = tags::cwt;
namespace codec = tags::codec;

struct custom_backend {
    static constexpr cwt::algorithm algorithm_id = cwt::algorithm::es256;
    static tags::expected<cwt::byte_string, tags::status_code>
    sign(void *key, std::span<const std::byte> to_be_signed);
    static tags::expected<void, tags::status_code>
    verify(void *key, std::span<const std::byte> to_be_signed,
           std::span<const std::byte> signature);
};
// cwt::sign1<custom_backend>(key, {}, {}, payload)
```

## Sign and verify

```cpp
#include <cbor_tags/cwt/cwt.h>

// key: a key accepted by custom_backend; bytes: encoded claims.
if (bytes) {
    auto message = cwt::sign1<custom_backend>(key, {}, {}, *bytes);
    if (message) {
        auto verified = cwt::verify_sign1<custom_backend>(key, *message);
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
auto message = cwt::sign<custom_backend>(key, {}, {}, *bytes);
if (message) {
    auto result = cwt::verify_sign<custom_backend>(key, *message);
}
```

Check `bytes` first. Use `add_signature` or `sign_signature` for more signatures.
`make_sign1_tbs` and `make_sign_tbs` produce the corresponding
[Sig_structure (§4.4)](https://www.rfc-editor.org/rfc/rfc9052.html#section-4.4).

`verify_sign1`, `verify_signature`, `verify_sign`, `make_sign1_tbs` and
`make_sign_tbs` accept borrowed models. Verification still parses protected
headers and allocates the bytes to be signed. Signing helpers return owning
results, and the backend interface is unchanged.

For a null payload, verification requires a detached payload. `std::nullopt`
means missing; an engaged empty span means a valid empty payload:

```cpp
auto result = cwt::verify_sign1<custom_backend>(
    key, detached_message, {}, std::span<const std::byte>{});
```

An embedded payload takes precedence over the detached argument.

## Header policy

- Signing helpers reject unprotected `alg`/`crit` and conflicting algorithms.
  Unknown noncritical headers are discarded. Applications enforce any stricter
  cross-bucket duplicate policy from [RFC 9052 §3](https://www.rfc-editor.org/rfc/rfc9052.html#section-3),
  checking unknown labels before discarding them. Known `kid` duplication can be
  checked after decoding:

```cpp
auto protected_map = cwt::decode_protected_header(message.protected_header);
if (!protected_map || (protected_map->kid && message.unprotected.kid)) {
    return reject_message();
}
```
