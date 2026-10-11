# COSE signing and verification

**Experimental:** COSE signing and verification APIs may change.

Include `cbor_tags/cwt/cwt.h` for these backend-independent helpers.
See [CWT and COSE models](cwt.md) for codec selection and payload lifetimes.

## Backend contract

The runnable [custom backend example](../examples/24_experimental_cwt_backend.cpp)
uses an application-owned key and OpenSSL as a replaceable provider. Its two
provider calls are the integration points for your crypto library or HSM SDK;
map provider failures to `unexpected<status_code>` and return success only after
signing or verification succeeds. The crypto examples require the optional
`CBOR_TAGS_ENABLE_CWT_OPENSSL` build flag.

Keys can be application-defined types; the helpers forward them to your backend.
The backend receives the encoded COSE `Sig_structure`, not just the payload or a
precomputed digest. For ES256, the provider applies SHA-256 and returns a 64-byte
`r || s` signature, converting its native signature format if necessary.

Provide an algorithm identifier and operations with these signatures:

```cpp
#include <cbor_tags/cwt/cwt.h>

using namespace cbor::tags;

struct custom_backend {
    static constexpr cwt::algorithm algorithm_id = cwt::algorithm::es256;
    static expected<std::vector<std::byte>, status_code>
    sign(void *key, std::span<const std::byte> to_be_signed);
    static expected<void, status_code>
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
results. `sign` and `sign1` copy the supplied payload into that result;
`add_signature` appends an owned signature to the existing message. AAD spans
borrow caller-owned storage for the call and are not retained in the message.
The helpers encode headers, payload and AAD into temporary bytes for signing or
verification; the backend receives views of those bytes for the duration of its
call. These helpers are not an allocation-free signing path.

For a null payload, verification requires a detached payload. `std::nullopt`
means missing; an engaged empty span means a valid empty payload:

```cpp
auto result = cwt::verify_sign1<custom_backend>(
    key, detached_message, {}, std::span<const std::byte>{});
```

An embedded payload takes precedence over the detached argument.

## Complete token with external AAD

[Example 25](../examples/25_experimental_cwt_token.cpp) builds a complete
CWT-tagged COSE_Sign1 token: encode claims, protect `alg` and `kid`, sign with
external AAD, serialize, decode, verify, then apply issuer, audience and expiry
policy. It uses the built-in OpenSSL backend; substitute the backend and key
from [example 24](../examples/24_experimental_cwt_backend.cpp) at the signing and
verification calls to use your own provider.

[Additional authenticated data (AAD)](https://www.rfc-editor.org/rfc/rfc9052.html#section-4.3)
is authenticated but absent from the token.
Both parties must supply identical bytes, transported separately or reconstructed
from agreed application context; this example binds the
token to one request purpose.

The example uses a key already trusted by the receiver, checks its protected key ID, and injects a
fixed Unix time to keep the demonstration reproducible. A service supplies its
current time and trusted key configuration; `kid` identifies a key but does not
establish trust. Signature verification and application claim validation are
separate steps: the example also rejects expiry at the exact deadline.

## Multiple signers

[Example 26](../examples/26_experimental_cwt_multi_signer.cpp) creates a
CWT-tagged COSE_Sign token with two independent ES256 keys. `sign` creates the
first signature; `add_signature` appends the second over the same payload and
external AAD. Each signature has its own protected algorithm and key ID.

The example's application policy requires exactly two signatures in an agreed
order and verifies each index with its corresponding trusted key. It rejects
a wrong key, a damaged signature and a missing signer. The no-index
`verify_sign(key, message, aad)` overload checks every signature using that one
key; use the indexed overload or `verify_signature` when signers have different
keys. Key selection and whether one, all or a threshold of signers is required
remain application policy. Apply the claim policy from example 25 after that
signature policy succeeds.

## Header policy

- Signing helpers reject unprotected `alg`/`crit` and conflicting algorithms.
  Unknown noncritical headers are discarded. Applications enforce any stricter
  policy for duplicate labels across protected and unprotected headers from [RFC 9052 §3](https://www.rfc-editor.org/rfc/rfc9052.html#section-3),
  checking unknown labels before discarding them. Known `kid` duplication can be
  checked after decoding:

```cpp
auto protected_map = cwt::decode_protected_header(message.protected_header);
if (!protected_map || (protected_map->kid && message.unprotected.kid)) {
    return reject_message();
}
```
