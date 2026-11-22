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
            .payload          = std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}},
            .signature        = std::vector<std::byte>(64, std::byte{0xA5}),
        };

        const auto to_be_signed = make_sign1_tbs(message);
        REQUIRE(to_be_signed);
        CHECK_EQ(to_hex(to_be_signed->flatten()), "846a5369676e61747572653143a101264043010203");

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
            encode_protected_header(header_map{.alg = std::nullopt, .kid = std::vector<std::byte>{std::byte{0x01}}, .crit = {}});
        REQUIRE(signature_protected);

        cose_sign message{
            .protected_header = *body_protected,
            .unprotected      = {},
            .payload          = std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}},
            .signatures       = {},
        };
        cose_signature signature{
            .protected_header = *signature_protected,
            .unprotected      = {},
            .signature        = std::vector<std::byte>(64, std::byte{0xA5}),
        };
        message.signatures.push_back(signature);

        const auto to_be_signed = make_sign_tbs(message, signature);
        REQUIRE(to_be_signed);
        CHECK_EQ(to_hex(to_be_signed->flatten()), "85695369676e617475726543a1012644a104410140420102");

        std::vector<std::byte> encoded;
        auto                   enc = make_encoder<codec::cwt>(encoded);
        REQUIRE(enc(as_cwt(as_cose_sign(message))));

        CHECK(encoded.size() > signature.signature.size());
        CHECK_EQ(to_hex(std::span<const std::byte>{encoded.data(), 9}), "d83dd8628443a10126");
    }

    TEST_CASE("segmented signing input uses exact byte string length boundaries") {
        for (const auto &[size, header] : std::array<std::pair<std::size_t, std::string_view>, 5>{
                 {{0, "40"}, {23, "57"}, {24, "5818"}, {255, "58ff"}, {256, "590100"}}}) {
            const cose_sign1 message{.protected_header = {},
                                     .unprotected      = {},
                                     .payload          = std::vector<std::byte>(size, std::byte{0xab}),
                                     .signature        = {}};
            auto             input = make_sign1_tbs(message);
            REQUIRE(input);
            std::string expected_hex = "846a5369676e6174757265314040";
            expected_hex += header;
            for (std::size_t i = 0; i < size; ++i) {
                expected_hex += "ab";
            }
            CHECK(to_hex(input->flatten()) == expected_hex);
        }
    }

    TEST_CASE("a header with many labels verifies and a label in both buckets is still refused") {
        // 19 hh ll 00 for every label: the label in 2 bytes, 0 as its value
        const auto labels = [](unsigned first, unsigned count) {
            static constexpr std::string_view digits{"0123456789abcdef"};
            std::string                       hex;
            for (unsigned label = first; label < first + count; ++label) {
                hex += "19";
                for (const auto shift : {12U, 8U, 4U, 0U}) {
                    hex += digits[(label >> shift) & 0xfU];
                }
                hex += "00";
            }
            return hex;
        };

        // [protected, {1000: 0 ... 20999: 0}, h'a0', h'00']. b9 4e 20 opens a map of 20,000
        const auto verify = [&](std::string_view protected_hex) {
            const auto bytes = to_bytes(std::string{"84"} + std::string{protected_hex} + "b94e20" + labels(1000, 20000) + "41a04100");

            cose_sign1 message;
            auto       decoder = make_decoder<codec::cwt>(bytes);
            auto       decoded = decoder(message);
            REQUIRE(decoded);
            return verify_sign1<signature_presence_es256_backend>(nullptr, message);
        };

        // {1: -7}
        CHECK(verify("43a10126"));

        // {1: -7, 20999: 0}: 20999 is in the unprotected map as well
        const auto both = verify("47a2012619520700");
        REQUIRE_FALSE(both);
        CHECK_EQ(both.error(), status_code::error);
    }

    TEST_CASE("every signature of a message with many body labels verifies") {
        // 19 hh ll 00 for the labels 1000 to 2999
        std::string body_labels;
        for (unsigned label = 1000; label < 3000; ++label) {
            static constexpr std::string_view digits{"0123456789abcdef"};
            body_labels += "19";
            for (const auto shift : {12U, 8U, 4U, 0U}) {
                body_labels += digits[(label >> shift) & 0xfU];
            }
            body_labels += "00";
        }

        // [h'', {2,000 labels}, h'a0', [100 times [h'a10126', {}, h'51']]]
        std::string hex = "8440b907d0" + body_labels + "41a09864";
        for (int signer = 0; signer < 100; ++signer) {
            hex += "8343a10126a04151";
        }
        const auto bytes = to_bytes(hex);

        cose_sign message;
        auto      decoder = make_decoder<codec::cwt>(bytes);
        auto      decoded = decoder(message);
        REQUIRE(decoded);
        REQUIRE_EQ(message.signatures.size(), 100U);

        CHECK(verify_sign<signature_presence_es256_backend>(nullptr, message));
        CHECK(verify_sign<signature_presence_es256_backend>(nullptr, message, std::size_t{99}));

        // An empty signature at the end is still found
        message.signatures.back().signature.clear();
        CHECK_FALSE(verify_sign<signature_presence_es256_backend>(nullptr, message));
    }

} // TEST_SUITE("cbor_wire/cwt")
