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

/**
token = #6.61(#6.18([
    protected: bstr .cbor header,
    unprotected: {},
    payload: bstr .cbor claims,
    signature: bstr .size 64
]))
header = { 1 => -7, 4 => bstr }
claims = { 1 => tstr, 2 => tstr, 3 => tstr, 4 => int }
*/
int main() {
    // Hold the claims encoded once for signing and transmission.
    std::vector<std::byte> claims_buffer;

    // Store outgoing headers and views separately from the receiver's input.
    byte_segments outgoing;
    std::vector<std::byte> received_buffer;

    // Define the claims that the receiver's policy will require.
    cwt::claims_set source{
        .issuer = "coap://as.example.com",
        .subject = "alice",
        .audience = std::string{"coap://sensor.example.com"},
        .expiration = std::int64_t{1893456000},
    };

    // Encode the claims into the payload buffer that later operations borrow.
    auto claims_encoder = make_encoder<codec::cwt>(claims_buffer);
    if (!claims_encoder(source)) {
        return 1;
    }

    // Generate an example key that the receiver already trusts.
    auto key = make_example_key();
    if (!key) {
        return 1;
    }

    // Protect the algorithm and the application's expected key identifier.
    const std::vector<std::byte> key_id{std::byte{1}};
    cwt::header_map header{
        .alg = cwt::algorithm::es256,
        .kid = key_id,
    };

    // Keep the encoded protected header alive alongside the payload.
    auto protected_bytes = cwt::encode_protected_header(header);
    if (!protected_bytes) {
        return 1;
    }

    // Additional authenticated data (AAD) binds the token to context outside it.
    constexpr std::string_view context = "sensor-api:v1:read-temperature";

    // Borrow the literal's bytes for the duration of signing.
    const auto external_aad = std::as_bytes(std::span{context.data(), context.size()});

    // Reserve signature storage owned by this application.
    std::array<std::byte, 64> signature{};

    // Refer to the existing encoded header and claims without copying either.
    cwt::cose_sign1_view message{
        .protected_header = *protected_bytes,
        .unprotected = {},
        .payload = claims_buffer,
        .signature = {},
    };

    // Write the signature directly into the application's buffer.
    auto written =
        cwt::sign1<cwt::openssl_es256_backend>(key.get(), message, signature, external_aad);
    if (!written) {
        return 1;
    }

    // Attach the completed signature as a view into that buffer.
    message.signature = std::span<const std::byte>{signature}.first(*written);

    // Produce ordered CBOR headers and views into the original token fields.
    auto token_encoder = make_encoder<codec::cwt>(outgoing);
    if (!token_encoder(cwt::as_cwt(cwt::as_cose_sign1(message)))) {
        return 1;
    }

    // Copy the outgoing segments once to simulate delivery into a receive buffer.
    outgoing.flatten_to(received_buffer);

    // Require both tags while borrowing the envelope's byte fields from received_buffer.
    cwt::cose_sign1_view received;
    auto signed_token = make_tag_pair(cwt::cose_sign1_tag{}, received);
    auto token = make_tag_pair(cwt::cwt_tag{}, signed_token);

    // Decode one complete token without copying its payload or signature.
    auto token_decoder = make_decoder<codec::cwt>(received_buffer);
    if (!token_decoder(token) || token_decoder.tell() != received_buffer.end()) {
        return 2;
    }

    // Reconstruct the same AAD from the receiver's independent context.
    constexpr std::string_view receiver_context = "sensor-api:v1:read-temperature";
    const auto receiver_aad =
        std::as_bytes(std::span{receiver_context.data(), receiver_context.size()});

    // Verify the borrowed bytes before interpreting the claims.
    if (!cwt::verify_sign1<cwt::openssl_es256_backend>(key.get(), received, receiver_aad)) {
        return 2;
    }

    // Borrow the authenticated key ID and require the expected key and embedded payload.
    cwt::header_map_view received_header;
    if (!cwt::decode_protected_header(received.protected_header, received_header) ||
        !received_header.kid || !std::ranges::equal(*received_header.kid, key_id) ||
        !received.payload) {
        return 2;
    }

    // Borrow claim strings from the verified receive buffer.
    cwt::claims_view claims;

    // Decode exactly one claims map after signature verification succeeds.
    auto claims_decoder = make_decoder<codec::cwt>(*received.payload);
    if (!claims_decoder(claims) || claims_decoder.tell() != received.payload->end()) {
        return 2;
    }

    // Require the issuer, subject, audience and expiration used by this application.
    if (claims.issuer != "coap://as.example.com" || claims.subject != "alice" || !claims.audience ||
        !claims.expiration) {
        return 2;
    }

    // Use a fixed clock value to keep the example reproducible.
    constexpr std::int64_t now = 1893455940;
    const auto *audience = std::get_if<std::string_view>(&*claims.audience);
    const auto *expiry = std::get_if<std::int64_t>(&*claims.expiration);
    if (!audience || *audience != "coap://sensor.example.com" || !expiry || now >= *expiry) {
        return 2;
    }

    // Retain only the subject if it must survive reuse of the receive buffer.
    const std::string retained_subject{*claims.subject};
    if (retained_subject != "alice") {
        return 2;
    }

    // Confirm that omitting the receiver's required AAD rejects the token.
    if (cwt::verify_sign1<cwt::openssl_es256_backend>(key.get(), received)) {
        return 3;
    }
    return 0;
}
