# Experimental countersignatures

An issuer signs a message; a notary countersigns it with separate external context ([example 33](../examples/33_experimental_cwt_countersignature.cpp)); build with `CBOR_TAGS_ENABLE_CWT_OPENSSL=ON`.

```cpp
// Experimental: countersignature APIs may change.
#include <array>
#include <cbor_tags/cwt/countersignatures.h>
#include <cbor_tags/cwt/openssl_crypto.h>
#include <memory>

using namespace cbor::tags;

// Each signer owns an independent P-256 key.
auto make_key() {
    return std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>{
        EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1"), EVP_PKEY_free};
}

/**
token = [bstr, {}, bstr, bstr]
countersignature = #6.19([bstr, {}, bstr])
*/
int main() {
    auto issuer = make_key();
    auto notary = make_key();
    if (!issuer || !notary) {
        return 1;
    }

    // The notary binds its own external context independently of the issuer.
    const std::array payload{std::byte{1}};
    const std::array notary_aad{std::byte{2}};
    auto token = cwt::sign1<cwt::openssl_esp256_backend>(issuer.get(), {}, {}, payload);
    if (!token) {
        return 1;
    }

    auto countersignature = cwt::countersign<cwt::openssl_esp256_backend>(notary.get(), *token,
        cwt::cose_signature{}, notary_aad);
    if (!countersignature) {
        return 1;
    }

    // Keep the standalone countersignature's transport buffer separate from its model.
    std::vector<std::byte> buffer;

    auto encoder = make_encoder<codec::cwt>(buffer);
    if (!encoder(cwt::as_countersignature(*countersignature))) {
        return 1;
    }

    auto received = cwt::as_countersignature(cwt::cose_signature{});
    auto decoder = make_decoder<codec::cwt>(buffer);
    if (!decoder(received)) {
        return 1;
    }

    // A countersignature does not replace verification of the original token.
    if (!cwt::verify_sign1<cwt::openssl_esp256_backend>(issuer.get(), *token) ||
        !cwt::verify_countersignature<cwt::openssl_esp256_backend>(notary.get(), *token,
            received.second, notary_aad)) {
        return 1;
    }
}
```

## Targets and storage

| Choice | Contract |
|---|---|
| Sign1 target | Binds original protected bytes, payload, and original signature. |
| Individual signature or countersignature target | Binds its protected bytes and signature; also verify the parent signature to authenticate its payload. |
| Whole COSE_Sign target | Unsupported. |
| Full countersignature | Uses its own protected algorithm. |
| Abbreviated countersignature | The application supplies its algorithm and key. |
| External context | Independent of the original signer's AAD and absent from the message. |
| Segmented signing input | CBOR headers interleaved with views into original data, in encoding order; keep the data stable until the backend finishes. |
| Parsed header output | Assigned after verification and critical processing; views borrow the countersignature's protected bytes. |

## Next examples

- [Caller storage and abbreviated countersignatures](../examples/34_experimental_cwt_abbreviated_countersignature.cpp).
- [Borrowed countersignature chains](../examples/35_experimental_cwt_countersignature_chain.cpp).

Chains explicitly select recursive headers; singleton/list inputs normalize to a nonempty collection. Application extra fields must not recursively contain the same header model.

Legacy full verification requires `verify_legacy_countersignature`: on Sign1 it does not bind the original signature. Legacy generation, abbreviated legacy operations, and implicit fallback are unsupported; see [RFC 9338](https://www.rfc-editor.org/rfc/rfc9338.html).
