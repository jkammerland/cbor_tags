// Experimental: CWT models and crypto APIs may change.
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_segments.h"
#include "cbor_tags/cwt/openssl_crypto.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <span>
#include <string>
#include <string_view>
#include <variant>
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

// Require two signatures in an agreed order, each paired with its trusted key.
bool verify_both_signers(const cwt::cose_sign_view &message, EVP_PKEY *first_key,
    EVP_PKEY *second_key, std::span<const std::byte> external_aad) {
    if (message.signatures.size() != 2) {
        return false;
    }

    // Authenticate each signature before accepting its protected key identifier.
    if (!cwt::verify_sign<cwt::openssl_esp256_backend>(first_key, message, std::size_t{0},
            external_aad) ||
        !cwt::verify_sign<cwt::openssl_esp256_backend>(second_key, message, std::size_t{1},
            external_aad)) {
        return false;
    }

    // Borrow both key identifiers from the authenticated protected headers.
    cwt::header_map_view first_header;
    cwt::header_map_view second_header;
    if (!cwt::decode_protected_header(message.signatures[0].protected_header, first_header) ||
        !cwt::decode_protected_header(message.signatures[1].protected_header, second_header)) {
        return false;
    }

    // Match the identifiers to this example's two configured signers.
    return first_header.kid && first_header.kid->size() == 1 &&
           first_header.kid->front() == std::byte{1} && second_header.kid &&
           second_header.kid->size() == 1 && second_header.kid->front() == std::byte{2};
}

/**
token = #6.61(#6.98([
    protected: bstr .size 0,
    unprotected: {},
    payload: bstr .cbor claims,
    signatures: [signature, signature]
]))
signature = [protected: bstr .cbor header, unprotected: {}, signature: bstr .size 64]
header = { 1 => -9, 4 => bstr }
claims = { 2 => tstr }
*/
int main() {
    // Hold the single claims payload shared by both signers.
    std::vector<std::byte> claims_buffer;

    // Store outgoing headers and views separately from the receiver's input.
    byte_segments outgoing;
    std::vector<std::byte> received_buffer;

    // Define the claim that both signers will authenticate.
    cwt::claims_set source{
        .subject = "alice",
    };

    // Encode the shared payload once before either signature is created.
    auto claims_encoder = make_encoder<codec::cwt>(claims_buffer);
    if (!claims_encoder(source)) {
        return 1;
    }

    // Generate two independent example keys that the receiver already trusts.
    auto first_key = make_example_key();
    auto second_key = make_example_key();
    if (!first_key || !second_key) {
        return 1;
    }

    // Protect each signer's algorithm and its configured key identifier.
    cwt::header_map first_header{
        .alg = cwt::algorithm::esp256,
        .kid = std::vector<std::byte>{std::byte{1}},
    };
    cwt::header_map second_header{
        .alg = cwt::algorithm::esp256,
        .kid = std::vector<std::byte>{std::byte{2}},
    };

    // Keep the two encoded headers alive while signing and sending their views.
    auto first_header_bytes = cwt::encode_protected_header(first_header);
    auto second_header_bytes = cwt::encode_protected_header(second_header);
    if (!first_header_bytes || !second_header_bytes) {
        return 1;
    }

    // Bind both signatures to additional authenticated data (AAD) absent from the token.
    constexpr std::string_view context = "sensor-api:v1:joint-approval";

    // Borrow the literal's bytes for both signing calls.
    const auto external_aad = std::as_bytes(std::span{context.data(), context.size()});

    // Store each signer's result in a separate application-owned buffer.
    std::array<std::byte, 64> first_signature{};
    std::array<std::byte, 64> second_signature{};

    // Borrow the one payload shared by all signatures.
    cwt::cose_sign_view message{
        .protected_header = {},
        .unprotected = {},
        .payload = claims_buffer,
        .signatures = {},
    };

    // Borrow each signer's encoded protected header.
    cwt::cose_signature_view first_signer{
        .protected_header = *first_header_bytes,
        .unprotected = {},
        .signature = {},
    };
    cwt::cose_signature_view second_signer{
        .protected_header = *second_header_bytes,
        .unprotected = {},
        .signature = {},
    };

    // Write the first signature without copying the shared payload.
    auto first_written = cwt::sign_signature<cwt::openssl_esp256_backend>(first_key.get(), message,
        first_signer, first_signature, external_aad);
    if (!first_written) {
        return 1;
    }

    // Write the second signature over the same payload and AAD.
    auto second_written = cwt::sign_signature<cwt::openssl_esp256_backend>(second_key.get(),
        message, second_signer, second_signature, external_aad);
    if (!second_written) {
        return 1;
    }

    // Attach signature views while the vector owns only their descriptors.
    first_signer.signature = std::span<const std::byte>{first_signature}.first(*first_written);
    second_signer.signature = std::span<const std::byte>{second_signature}.first(*second_written);
    message.signatures = {first_signer, second_signer};

    // Produce ordered CBOR headers and views into the existing token fields.
    auto token_encoder = make_encoder<codec::cwt>(outgoing);
    if (!token_encoder(cwt::as_cwt(cwt::as_cose_sign(message)))) {
        return 1;
    }

    // Copy segments once to simulate delivery into an independently owned receive buffer.
    outgoing.flatten_to(received_buffer);

    // Require the tags and borrow each signature's bytes from the receive buffer.
    cwt::cose_sign_view received;
    auto signed_token = make_tag_pair(cwt::cose_sign_tag{}, received);
    auto token = make_tag_pair(cwt::cwt_tag{}, signed_token);

    // Decode exactly one complete multi-signer token.
    auto token_decoder = make_decoder<codec::cwt>(received_buffer);
    if (!token_decoder(token) || token_decoder.tell() != received_buffer.end()) {
        return 2;
    }

    // Reconstruct the receiver's identical AAD independently of the token.
    constexpr std::string_view receiver_context = "sensor-api:v1:joint-approval";
    const auto receiver_aad =
        std::as_bytes(std::span{receiver_context.data(), receiver_context.size()});

    // Require both trusted signers before interpreting the claims.
    if (!verify_both_signers(received, first_key.get(), second_key.get(), receiver_aad) ||
        !received.payload) {
        return 2;
    }

    // Choose borrowed text for the receiver's claims.
    cwt::claims_view claims;

    // Decode the authenticated payload and require this application's subject.
    auto claims_decoder = make_decoder<codec::cwt>(*received.payload);
    if (!claims_decoder(claims) || claims_decoder.tell() != received.payload->end() ||
        claims.subject != "alice") {
        return 2;
    }

    // Reject using the first trusted key in place of the second signer.
    if (verify_both_signers(received, first_key.get(), first_key.get(), receiver_aad)) {
        return 3;
    }

    // Reject a token whose descriptors omit one required signature.
    auto missing_signer = received;
    missing_signer.signatures.pop_back();
    if (verify_both_signers(missing_signer, first_key.get(), second_key.get(), receiver_aad)) {
        return 3;
    }

    // Change the final signature byte in the owning receive buffer to prove verification fails.
    received_buffer.back() ^= std::byte{1};
    if (verify_both_signers(received, first_key.get(), second_key.get(), receiver_aad)) {
        return 3;
    }
    return 0;
}
