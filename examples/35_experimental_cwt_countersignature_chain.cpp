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
countersignature = [bstr, { ? 11 => countersignature / [+ countersignature] }, bstr]
*/
int main() {
    auto issuer = make_key();
    auto notary = make_key();
    if (!issuer || !notary) {
        return 1;
    }

    const std::array payload{std::byte{1}};
    auto token = cwt::sign1<cwt::openssl_esp256_backend>(issuer.get(), {}, {}, payload);
    if (!token) {
        return 1;
    }

    // Select recursive headers only for messages that need countersignature chains.
    using headers = cwt::countersignature_headers<>;
    auto first = cwt::countersign<cwt::openssl_esp256_backend>(notary.get(), *token,
        headers::signature_type{});
    if (!first) {
        return 1;
    }

    auto second = cwt::countersign<cwt::openssl_esp256_backend>(issuer.get(), *first,
        headers::signature_type{});
    if (!second) {
        return 1;
    }
    first->unprotected.extra.full.emplace().signatures.push_back(*second);

    std::vector<std::byte> buffer;

    auto encoder = make_encoder<codec::cwt>(buffer);
    if (!encoder(*first)) {
        return 1;
    }

    cwt::countersignature_headers_view::signature_type received;
    auto decoder = make_decoder<codec::cwt>(buffer);
    if (!decoder(received) || !received.unprotected.extra.full) {
        return 1;
    }

    // Verify every required layer; decoding alone grants no trust.
    const auto &chain = received.unprotected.extra.full->signatures.front();
    if (!cwt::verify_sign1<cwt::openssl_esp256_backend>(issuer.get(), *token) ||
        !cwt::verify_countersignature<cwt::openssl_esp256_backend>(notary.get(), *token,
            received) ||
        !cwt::verify_countersignature<cwt::openssl_esp256_backend>(issuer.get(), received, chain)) {
        return 1;
    }
}
