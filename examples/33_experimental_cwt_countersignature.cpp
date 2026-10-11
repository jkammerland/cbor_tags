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
