// Experimental: CWT models and crypto backend APIs may change.
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/codec/cwt.h"
#include "cbor_tags/cwt/openssl_crypto.h"

#include <cstddef>
#include <memory>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <span>
#include <vector>

using namespace cbor::tags;

// Keep ownership of the provider's key in an application-defined type.
struct application_key {
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> handle{nullptr, EVP_PKEY_free};
};

struct application_backend {
    static constexpr cwt::algorithm algorithm_id = cwt::algorithm::es256;

    // Replace this provider call with your library's ES256 signing operation.
    static expected<std::vector<std::byte>, status_code> sign(const application_key &key,
        std::span<const std::byte> to_be_signed) {
        return cwt::openssl_es256_backend::sign(key.handle.get(), to_be_signed);
    }

    // Replace this provider call with your library's ES256 verification operation.
    static expected<void, status_code> verify(const application_key &key,
        std::span<const std::byte> to_be_signed, std::span<const std::byte> signature) {
        return cwt::openssl_es256_backend::verify(key.handle.get(), to_be_signed, signature);
    }
};

// Generate a temporary P-256 key for this executable example.
application_key make_example_key() {
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> context{
        EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr), EVP_PKEY_CTX_free};
    if (!context || EVP_PKEY_keygen_init(context.get()) != 1 ||
        EVP_PKEY_CTX_set_ec_paramgen_curve_nid(context.get(), NID_X9_62_prime256v1) != 1) {
        return {};
    }

    EVP_PKEY *generated = nullptr;
    const auto result = EVP_PKEY_keygen(context.get(), &generated);
    application_key key;
    key.handle.reset(generated);
    if (result != 1) {
        return {};
    }
    return key;
}

/**
claims = { 2 => tstr }
*/
int main() {
    // Hold the encoded claims passed to the signing helper.
    std::vector<std::byte> buffer;

    // Store the subject in a claims_set that owns its text.
    cwt::claims_set source{
        .subject = "alice",
    };

    // Encode the claims as the COSE message's payload.
    auto encoder = make_encoder<codec::cwt>(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // Keep the application's key alive throughout signing and verification.
    auto key = make_example_key();
    if (!key.handle) {
        return 1;
    }

    // Sign through the application backend, copying the claims into the returned message.
    auto message = cwt::sign1<application_backend>(key, {}, {}, buffer);
    if (!message) {
        return 1;
    }

    // Verify using the same backend and the key's public component.
    if (!cwt::verify_sign1<application_backend>(key, *message)) {
        return 2;
    }

    // Confirm that changing the signed payload invalidates its signature.
    if (!message->payload || message->payload->empty()) {
        return 2;
    }
    message->payload->back() ^= std::byte{1};
    if (cwt::verify_sign1<application_backend>(key, *message)) {
        return 2;
    }
    return 0;
}
