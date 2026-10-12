// Experimental: confirmation and crypto APIs may change.
#include "cwt_key_sample.h"

#include <cassert>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cwt/openssl_keys.h>

/**
 * token = #6.61(#6.18([bstr .cbor {1: -9}, {}, bstr .cbor claims, bstr .size 64]))
 * claims = { 1: tstr, 8: {1: public-key, 5: bstr .size 32} }
 * public-key = { 1: 2, -1: 1, -2: bstr .size 32, -3: bstr .size 32, 3: -9, 4: [2] }
 */
int main() {
    using namespace cbor::tags;

    auto issuer_key = make_issuer_key();
    if (!issuer_key) {
        return 1;
    }

    // The issuer names a separately provisioned holder key in the confirmation claim.
    cwt::confirmation<cwt::ec2_public_key> confirmation;
    confirmation.key = example_holder_key();
    auto thumbprint = cwt::key_thumbprint<cwt::openssl_sha256>(*confirmation.key);
    if (!thumbprint) {
        return 1;
    }
    confirmation.thumbprint = std::vector<std::byte>{thumbprint->begin(), thumbprint->end()};
    std::string issuer = "https://issuer.example";
    auto claims = cwt::fields(cwt::field<1>(issuer), cwt::field<8>(confirmation));

    std::vector<std::byte> payload;
    std::vector<std::byte> token;

    auto claims_encoder = make_encoder<codec::cwt>(payload);
    if (!claims_encoder(claims)) {
        return 1;
    }

    // External context must be supplied unchanged by both sender and receiver.
    std::string_view context = "confirmation-example:v1";
    auto aad = std::as_bytes(std::span{context.data(), context.size()});
    cwt::header_map headers{.alg = cwt::algorithm::esp256};
    auto signed_message =
        cwt::sign1<cwt::openssl_esp256_backend>(issuer_key.get(), headers, {}, payload, aad);
    if (!signed_message) {
        return 1;
    }

    auto encoder = make_encoder<codec::cwt>(token);
    if (!encoder(cwt::cwt_tag{}, cwt::cose_sign1_tag{}, *signed_message)) {
        return 1;
    }

    // Authenticate the token before interpreting its confirmation claim.
    cwt::cwt_tag cwt_tag;
    cwt::cose_sign1_tag sign1_tag;
    cwt::cose_sign1_view received;
    auto decoder = make_decoder<codec::cwt>(token);
    if (!decoder(cwt_tag, sign1_tag, received) ||
        !cwt::verify_sign1<cwt::openssl_esp256_backend>(issuer_key.get(), received, aad)) {
        return 1;
    }

    std::string_view received_issuer;
    cwt::confirmation_view<cwt::ec2_public_key_view> received_confirmation;
    auto received_claims =
        cwt::fields(cwt::field<1>(received_issuer), cwt::field<8>(received_confirmation));
    auto claims_decoder = make_decoder<codec::cwt>(*received.payload);
    if (!claims_decoder(received_claims) || received_issuer != issuer ||
        !received_confirmation.key || !received_confirmation.thumbprint) {
        return 1;
    }

    // Both confirmation methods must identify the same public key.
    auto received_thumbprint = cwt::key_thumbprint<cwt::openssl_sha256>(*received_confirmation.key);
    if (!received_thumbprint ||
        !std::ranges::equal(*received_thumbprint, *received_confirmation.thumbprint)) {
        return 1;
    }

    // Import checks verification permissions; the application still needs a holder proof protocol.
    auto holder_key = cwt::import_p256_public_key(*received_confirmation.key);
    if (!holder_key) {
        return 1;
    }

    assert(holder_key->get() != nullptr);
}
