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
abbreviated-counter = bstr
*/
int main() {
    auto issuer = make_key();
    auto notary = make_key();
    if (!issuer || !notary) {
        return 1;
    }

    const std::array payload{std::byte{1}};
    const std::array notary_aad{std::byte{2}};
    auto token = cwt::sign1<cwt::openssl_esp256_backend>(issuer.get(), {}, {}, payload);
    if (!token) {
        return 1;
    }

    // Caller-owned signature storage avoids allocating the result.
    std::array<std::byte, 64> storage;

    auto written = cwt::countersign0<cwt::openssl_esp256_backend>(notary.get(), *token,
        std::span<std::byte>{storage}, notary_aad);
    if (!written) {
        return 1;
    }

    // The application supplies the abbreviated counter's algorithm and key.
    auto signature = std::span<const std::byte>{storage}.first(*written);
    if (!cwt::verify_sign1<cwt::openssl_esp256_backend>(issuer.get(), *token) ||
        !cwt::verify_countersignature0<cwt::openssl_esp256_backend>(notary.get(), *token, signature,
            notary_aad)) {
        return 1;
    }
}
