// Experimental: CWT models, codecs and crypto APIs may change.
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/codec/cwt.h"
#include "cbor_tags/cwt/openssl_crypto.h"

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

// Apply this application's required issuer, single audience and integer expiry policy.
bool accepts_claims(const cwt::claims_set &claims, std::int64_t now) {
    if (claims.issuer != "coap://as.example.com" || !claims.audience || !claims.expiration) {
        return false;
    }

    const auto *audience = std::get_if<std::string>(&*claims.audience);
    const auto *expiration = std::get_if<std::int64_t>(&*claims.expiration);
    return audience && *audience == "coap://sensor.example.com" && expiration && now < *expiration;
}

/**
token = #6.61(#6.18([
    protected: bstr .cbor protected-header,
    unprotected: {},
    payload: bstr .cbor claims,
    signature: bstr .size 64
]))
protected-header = { 1 => -7, 4 => bstr }
claims = { 1 => tstr, 2 => tstr, 3 => tstr, 4 => int }
*/
int main() {
    // Keep the encoded claims and complete token in separate buffers.
    std::vector<std::byte> claims_buffer;
    std::vector<std::byte> token_buffer;

    // Define the issuer, subject, intended recipient and expiration in Unix seconds.
    cwt::claims_set source{
        .issuer = "coap://as.example.com",
        .subject = "alice",
        .audience = std::string{"coap://sensor.example.com"},
        .expiration = std::int64_t{1893456000},
    };

    // Encode the claims that will be carried inside the signed token.
    auto claims_encoder = make_encoder<codec::cwt>(claims_buffer);
    if (!claims_encoder(source)) {
        return 1;
    }

    // Use one key that the receiver already trusts for this example.
    auto key = make_example_key();
    if (!key) {
        return 1;
    }

    // Additional authenticated data (AAD) binds the signature to context outside the token.
    constexpr std::string_view request_context = "sensor-api:v1:read-temperature";

    // Borrow the context bytes from a string literal that remains alive throughout the program.
    const auto external_aad =
        std::as_bytes(std::span{request_context.data(), request_context.size()});

    // Set the algorithm and key ID in the headers authenticated by the signature.
    const std::vector<std::byte> key_id{std::byte{1}};
    cwt::header_map protected_header{
        .alg = cwt::algorithm::es256,
        .kid = key_id,
    };

    // Sign with AAD, copying claims into a message that owns its payload, headers and signature.
    auto message = cwt::sign1<cwt::openssl_es256_backend>(key.get(), protected_header, {},
        claims_buffer, external_aad);
    if (!message) {
        return 1;
    }

    // Write the tags and signed message into the token buffer.
    auto token_encoder = make_encoder<codec::cwt>(token_buffer);
    if (!token_encoder(cwt::as_cwt(cwt::as_cose_sign1(*message)))) {
        return 1;
    }

    // Require the CWT and COSE_Sign1 tags when decoding into a message that owns its data.
    cwt::cose_sign1 received;
    auto signed_token = make_tag_pair(cwt::cose_sign1_tag{}, received);
    auto token = make_tag_pair(cwt::cwt_tag{}, signed_token);

    // Decode one complete token, copying its headers, payload and signature from the buffer.
    auto token_decoder = make_decoder<codec::cwt>(token_buffer);
    if (!token_decoder(token) || token_decoder.tell() != token_buffer.end()) {
        return 2;
    }

    // Reconstruct identical AAD bytes from the receiver's context and verify with its trusted key.
    constexpr std::string_view receiver_context = "sensor-api:v1:read-temperature";
    const auto receiver_aad =
        std::as_bytes(std::span{receiver_context.data(), receiver_context.size()});
    if (!cwt::verify_sign1<cwt::openssl_es256_backend>(key.get(), received, receiver_aad)) {
        return 2;
    }

    // Require the authenticated key ID and an embedded claims payload.
    auto headers = cwt::decode_protected_header(received.protected_header);
    if (!headers || headers->kid != key_id || !received.payload) {
        return 2;
    }

    // Store the decoded claims in a claims_set that owns its strings.
    cwt::claims_set claims;

    // After verification, decode the claims and copy their strings from the message payload.
    auto claims_decoder = make_decoder<codec::cwt>(*received.payload);
    if (!claims_decoder(claims) || claims_decoder.tell() != received.payload->end()) {
        return 2;
    }

    // Inject a fixed clock value here where a service would supply its current Unix time.
    constexpr std::int64_t now = 1893455940;
    if (!accepts_claims(claims, now) || claims.subject != "alice") {
        return 2;
    }

    // Confirm that a different request context cannot verify the same token.
    constexpr std::string_view other_context = "sensor-api:v1:write-temperature";
    const auto wrong_aad = std::as_bytes(std::span{other_context.data(), other_context.size()});
    if (cwt::verify_sign1<cwt::openssl_es256_backend>(key.get(), received, wrong_aad)) {
        return 3;
    }

    // Confirm that omitting the required external AAD also fails verification.
    if (cwt::verify_sign1<cwt::openssl_es256_backend>(key.get(), received)) {
        return 3;
    }

    // Confirm that modifying the decoded payload invalidates the signature.
    if (received.payload->empty()) {
        return 3;
    }
    received.payload->back() ^= std::byte{1};
    if (cwt::verify_sign1<cwt::openssl_es256_backend>(key.get(), received, receiver_aad)) {
        return 3;
    }

    // Confirm that application policy rejects expiry at the exact deadline.
    if (accepts_claims(claims, 1893456000)) {
        return 3;
    }
    return 0;
}
