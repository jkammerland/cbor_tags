#include "cwt_signing_test_support.h"
#include "test_util.h"

using namespace cwt_test;

TEST_SUITE("cbor_wire/cwt") {

    TEST_CASE("COSE protected header and Sign1 Sig_structure encode in RFC shape") {
        const auto protected_header = encode_protected_header(header_map{.alg = algorithm::es256, .kid = std::nullopt, .crit = {}});
        REQUIRE(protected_header);
        CHECK_EQ(to_hex(*protected_header), "a10126");

        cose_sign1 message{
            .protected_header = *protected_header,
            .unprotected      = {},
            .payload          = byte_string{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}},
            .signature        = byte_string(64, std::byte{0xA5}),
        };

        const auto to_be_signed = make_sign1_tbs(message);
        REQUIRE(to_be_signed);
        CHECK_EQ(to_hex(*to_be_signed), "846a5369676e61747572653143a101264043010203");

        std::vector<std::byte> encoded;
        auto                   enc = make_encoder<codec::cwt>(encoded);
        REQUIRE(enc(as_cwt(as_cose_sign1(message))));

        CHECK(encoded.size() > message.signature.size());
        CHECK_EQ(to_hex(std::span<const std::byte>{encoded.data(), 8}), "d83dd28443a10126");
    }

    TEST_CASE("COSE Sign Sig_structure uses body and signature protected headers") {
        const auto body_protected = encode_protected_header(header_map{.alg = algorithm::es256, .kid = std::nullopt, .crit = {}});
        REQUIRE(body_protected);
        const auto signature_protected =
            encode_protected_header(header_map{.alg = std::nullopt, .kid = byte_string{std::byte{0x01}}, .crit = {}});
        REQUIRE(signature_protected);

        cose_sign message{
            .protected_header = *body_protected,
            .unprotected      = {},
            .payload          = byte_string{std::byte{0x01}, std::byte{0x02}},
            .signatures       = {},
        };
        cose_signature signature{
            .protected_header = *signature_protected,
            .unprotected      = {},
            .signature        = byte_string(64, std::byte{0xA5}),
        };
        message.signatures.push_back(signature);

        const auto to_be_signed = make_sign_tbs(message, signature);
        REQUIRE(to_be_signed);
        CHECK_EQ(to_hex(*to_be_signed), "85695369676e617475726543a1012644a104410140420102");

        std::vector<std::byte> encoded;
        auto                   enc = make_encoder<codec::cwt>(encoded);
        REQUIRE(enc(as_cwt(as_cose_sign(message))));

        CHECK(encoded.size() > signature.signature.size());
        CHECK_EQ(to_hex(std::span<const std::byte>{encoded.data(), 9}), "d83dd8628443a10126");
    }

} // TEST_SUITE("cbor_wire/cwt")
