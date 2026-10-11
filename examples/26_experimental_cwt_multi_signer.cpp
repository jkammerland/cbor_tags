// Experimental: CWT models, codecs and crypto APIs may change.
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/codec/cwt.h"
#include "cbor_tags/cwt/openssl_crypto.h"

#include <cstddef>
#include <memory>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <span>
#include <string_view>
#include <vector>

using namespace cbor::tags;

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

// Require both configured signers in this application's agreed order.
bool verify_both_signers(const cwt::cose_sign &message, EVP_PKEY *first_key, EVP_PKEY *second_key,
    std::span<const std::byte> external_aad) {
    if (message.signatures.size() != 2) {
        return false;
    }

    // Match each protected key ID to a key the receiver already trusts.
    auto first_header = cwt::decode_protected_header(message.signatures[0].protected_header);
    auto second_header = cwt::decode_protected_header(message.signatures[1].protected_header);
    if (!first_header || !second_header ||
        first_header->kid != std::vector<std::byte>{std::byte{1}} ||
        second_header->kid != std::vector<std::byte>{std::byte{2}}) {
        return false;
    }

    // Verify each signature with its own key over the shared payload and AAD.
    return cwt::verify_sign<cwt::openssl_es256_backend>(first_key, message, std::size_t{0},
               external_aad) &&
           cwt::verify_sign<cwt::openssl_es256_backend>(second_key, message, std::size_t{1},
               external_aad);
}

/**
token = #6.61(#6.98([
    protected: bstr .size 0,
    unprotected: {},
    payload: bstr .cbor claims,
    signatures: [signature, signature]
]))
signature = [protected: bstr .cbor signer-header, unprotected: {}, signature: bstr .size 64]
signer-header = { 1 => -7, 4 => bstr }
claims = { 2 => tstr }
*/
int main() {
    // Hold the encoded claims and the complete multi-signer token separately.
    std::vector<std::byte> claims_buffer;
    std::vector<std::byte> token_buffer;

    // Define the claim that both signers will authenticate.
    cwt::claims_set source{
        .subject = "alice",
    };

    // Encode the shared claims payload once.
    auto claims_encoder = make_encoder<codec::cwt>(claims_buffer);
    if (!claims_encoder(source)) {
        return 1;
    }

    // Generate two independent keys representing signers the receiver already trusts.
    auto first_key = make_example_key();
    auto second_key = make_example_key();
    if (!first_key || !second_key) {
        return 1;
    }

    // Additional authenticated data (AAD) binds both signatures to context outside the token.
    constexpr std::string_view context = "sensor-api:v1:joint-approval";

    // Borrow these context bytes; the receiver must independently supply identical bytes.
    const auto external_aad = std::as_bytes(std::span{context.data(), context.size()});

    // Set the first signer's algorithm and key ID in headers authenticated by its signature.
    cwt::header_map first_header{
        .alg = cwt::algorithm::es256,
        .kid = std::vector<std::byte>{std::byte{1}},
    };

    // Copy the claims into a COSE_Sign message that owns its payload, headers and signatures.
    auto message = cwt::sign<cwt::openssl_es256_backend>(first_key.get(), {}, {}, claims_buffer,
        first_header, {}, external_aad);
    if (!message) {
        return 1;
    }

    // Set the second signer's authenticated headers with a distinct key ID.
    cwt::header_map second_header{
        .alg = cwt::algorithm::es256,
        .kid = std::vector<std::byte>{std::byte{2}},
    };

    // Append a signature; the message owns it and retains its existing payload and headers.
    if (!cwt::add_signature<cwt::openssl_es256_backend>(second_key.get(), *message, second_header,
            {}, external_aad)) {
        return 1;
    }

    // Encode a CWT containing COSE_Sign tag 98 instead of COSE_Sign1 tag 18.
    auto token_encoder = make_encoder<codec::cwt>(token_buffer);
    if (!token_encoder(cwt::as_cwt(cwt::as_cose_sign(*message)))) {
        return 1;
    }

    // Require the CWT and COSE_Sign tags when decoding into a message that owns its data.
    cwt::cose_sign received;
    auto signed_token = make_tag_pair(cwt::cose_sign_tag{}, received);
    auto token = make_tag_pair(cwt::cwt_tag{}, signed_token);

    // Decode one complete token, copying its headers, payload and signatures from the buffer.
    auto token_decoder = make_decoder<codec::cwt>(token_buffer);
    if (!token_decoder(token) || token_decoder.tell() != token_buffer.end()) {
        return 2;
    }

    // Require both signatures before reading the claims.
    if (!verify_both_signers(received, first_key.get(), second_key.get(), external_aad) ||
        !received.payload) {
        return 2;
    }

    // Store the decoded claims in a claims_set that owns its strings.
    cwt::claims_set claims;

    // Copy the authenticated subject from the message payload and check its value.
    auto claims_decoder = make_decoder<codec::cwt>(*received.payload);
    if (!claims_decoder(claims) || claims_decoder.tell() != received.payload->end() ||
        claims.subject != "alice") {
        return 2;
    }

    // Confirm that the first key cannot verify the second signer's signature.
    if (cwt::verify_sign<cwt::openssl_es256_backend>(first_key.get(), received, std::size_t{1},
            external_aad)) {
        return 3;
    }

    // Confirm that one damaged signature fails the both-signers policy.
    auto damaged = received;
    if (damaged.signatures[1].signature.empty()) {
        return 3;
    }
    damaged.signatures[1].signature.back() ^= std::byte{1};
    if (verify_both_signers(damaged, first_key.get(), second_key.get(), external_aad)) {
        return 3;
    }

    // Confirm that removing a signature cannot bypass the required signer count.
    received.signatures.pop_back();
    if (verify_both_signers(received, first_key.get(), second_key.get(), external_aad)) {
        return 3;
    }
    return 0;
}
