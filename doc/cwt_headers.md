# Experimental COSE headers

Put an issuer in both the protected header and payload, then verify and compare them ([example 32](../examples/32_experimental_cwt_header_claims.cpp)); build with `CBOR_TAGS_ENABLE_CWT_OPENSSL=ON`.

```cpp
// Experimental: CWT header APIs may change.
#include <array>
#include <cbor_tags/cwt/openssl_crypto.h>
#include <memory>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <optional>
#include <string>
#include <vector>

using namespace cbor::tags;

struct identity {
    std::string issuer;
};

template <typename Encoder> auto encode(Encoder &encoder, const identity &value) {
    auto fields = cwt::fields(cwt::field<1>(value.issuer));
    return encoder(fields);
}

template <typename Decoder> auto decode(Decoder &decoder, identity &&value) {
    auto fields = cwt::fields(cwt::field<1>(value.issuer));
    return decoder(fields);
}

struct application_headers {
    std::optional<identity> claims;
};

auto cwt_fields(application_headers &value) {
    return cwt::fields(cwt::field<15>(value.claims));
}

auto cwt_fields(const application_headers &value) {
    return cwt::fields(cwt::field<15>(value.claims));
}

// Generate a temporary P-256 key for this executable example.
std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> make_example_key() {
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> context{
        EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr), EVP_PKEY_CTX_free};
    if (!context || EVP_PKEY_keygen_init(context.get()) != 1 ||
        EVP_PKEY_CTX_set_ec_paramgen_curve_nid(context.get(), NID_X9_62_prime256v1) != 1) {
        return {nullptr, EVP_PKEY_free};
    }

    EVP_PKEY *generated = nullptr;
    const auto result = EVP_PKEY_keygen(context.get(), &generated);
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{generated, EVP_PKEY_free};
    if (result != 1) {
        return {nullptr, EVP_PKEY_free};
    }
    return key;
}

/**
message = [
    protected: bstr .cbor protected-header,
    unprotected: {},
    payload: bstr .cbor claims,
    signature: bstr .size 64
]
protected-header = { 1 => -9, 15 => claims, 16 => "application/example+cwt" }
claims = { 1 => tstr }
*/
int main() {
    // This profile repeats the issuer in the protected header and payload.
    identity source{"issuer.example"};
    cwt::basic_header_map<false, application_headers> headers;
    headers.alg = cwt::algorithm::esp256;
    headers.typ = std::string{"application/example+cwt"};
    headers.extra.claims = source;

    std::vector<std::byte> payload;

    auto encoder = make_encoder<codec::cwt>(payload);
    if (!encoder(source)) {
        return 1;
    }

    auto protected_bytes = cwt::encode_protected_header(headers);
    if (!protected_bytes) {
        return 1;
    }

    auto key = make_example_key();
    if (!key) {
        return 1;
    }

    // Sign the protected claims using caller-owned signature storage.
    cwt::cose_sign1 message;
    message.protected_header = std::move(*protected_bytes);
    message.payload = std::move(payload);

    std::array<std::byte, 64> signature;
    cwt::basic_header_map<false, application_headers> signing_headers;
    auto written =
        cwt::sign1<cwt::openssl_esp256_backend>(key.get(), message, signing_headers, signature);
    if (!written) {
        return 1;
    }
    message.signature.assign(signature.begin(), signature.begin() + *written);

    // Verification commits parsed headers only after the signature and critical processing succeed.
    cwt::basic_header_map<false, application_headers> authenticated;
    const auto unsupported_critical = [](auto, auto, const auto &) -> expected<void, status_code> {
        return cbor::tags::unexpected<status_code>{status_code::error};
    };
    if (!cwt::verify_sign1<cwt::openssl_esp256_backend>(key.get(), message, authenticated,
            unsupported_critical)) {
        return 2;
    }

    // The application requires its media type and compares overlapping claims semantically.
    if (!authenticated.typ ||
        *authenticated.typ !=
            std::variant<std::uint64_t, std::string>{std::string{"application/example+cwt"}} ||
        !authenticated.extra.claims || !message.payload) {
        return 2;
    }

    identity received;
    auto decoder = make_decoder<codec::cwt>(*message.payload);
    if (!decoder(received) || decoder.tell() != message.payload->end()) {
        return 2;
    }

    return received.issuer == authenticated.extra.claims->issuer ? 0 : 2;
}
```

## Verification rules

The protected algorithm must match the backend. A profile that authenticates its
algorithm outside the message can pass `implicit_algorithm_profile` immediately
after the message (or after the signature/index for COSE_Sign), before the parsed
destinations. This requires the protected algorithm to be absent. Algorithm
admission happens before cryptographic verification, critical processing, and
publication of parsed headers.

| Step | Contract |
|---|---|
| Parse | Use original protected bytes and the selected codecs; reject misplaced or duplicate labels. |
| Verify | Check the signature before processing critical extensions. |
| Process critical labels | The callback must reject extensions it does not understand. |
| Assign output | Parsed headers are assigned only after verification and critical processing succeed. |
| Check application policy | Check type, issuer, audience, expiry, and overlapping claims as required by the application profile. |
| Borrow headers | Keep the message and backing bytes alive; output objects must not own the input bytes. |
| Write a signature | Caller output must not overlap signed input. |

Unknown values are discarded, but their labels remain available for duplicate checks; re-encoding emits only selected values.

Start with [type headers](../examples/31_experimental_cwt_type.cpp) for a smaller example; use [field bindings](cwt_fields.md) for additional labels. COSE_Sign supplies separate body and signer outputs and identifies their scope to the critical-label callback.
