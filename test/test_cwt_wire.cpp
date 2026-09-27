#include "cwt_test_support.h"
#include "test_util.h"

#include <list>
#include <ranges>

namespace {
template <typename... Items> expected<byte_string, status_code> encode_header_items(std::uint64_t size, Items &&...items) {
    byte_string encoded;
    auto        enc    = make_encoder(encoded);
    auto        result = enc(as_map{size}, std::forward<Items>(items)...);
    if (!result) {
        return cbor::tags::unexpected<status_code>{result.error()};
    }
    return encoded;
}

} // namespace

using namespace cwt_test;

TEST_SUITE("cbor_wire/cwt") {

    TEST_CASE("CWT claims encode with registered integer claim keys") {
        claims_set claims{
            .issuer     = "coap://as.example.com",
            .subject    = "erikw",
            .audience   = "coap://light.example.com",
            .expiration = std::int64_t{1444064944},
            .not_before = std::int64_t{1443944944},
            .issued_at  = std::int64_t{1443944944},
            .cwt_id     = byte_string{std::byte{0x0b}, std::byte{0x71}},
        };

        std::vector<std::byte> encoded;
        auto                   enc = make_encoder(encoded);
        REQUIRE(enc(claims));

        CHECK_EQ(to_hex(encoded),
                 "a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b77037818636f61703a2f2f6c696768742e6578616d706c652e6"
                 "36f6d041a5612aeb0051a5610d9f0061a5610d9f007420b71");
    }

    TEST_CASE("CWT NumericDate rejects non-finite values") {
        constexpr std::array malformed_dates{"a104fb7ff0000000000000", "a104fbfff0000000000000", "a104fb7ff8000000000000"};
        for (const auto hex : malformed_dates) {
            CAPTURE(hex);
            auto       input = to_bytes(hex);
            claims_set decoded;
            decoded.expiration = std::int64_t{42};

            auto result = make_decoder(input)(decoded);
            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), status_code::error);
            CHECK_EQ(decoded.expiration, numeric_date{std::int64_t{42}});
        }
    }

    TEST_CASE("COSE protected headers reject invalid critical label sets") {
        auto check_rejected = [](expected<byte_string, status_code> encoded) {
            REQUIRE(encoded);
            auto decoded = decode_protected_header(*encoded);
            REQUIRE_FALSE(decoded);
            CHECK_EQ(decoded.error(), status_code::error);
        };

        SUBCASE("empty critical list") { check_rejected(encode_header_items(1, std::uint64_t{2}, std::vector<int>{})); }

        SUBCASE("duplicate critical label") {
            check_rejected(encode_header_items(2, std::uint64_t{1}, algorithm::es256, std::uint64_t{2}, std::vector<int>{1, 1}));
        }

        SUBCASE("critical target is absent") { check_rejected(encode_header_items(1, std::uint64_t{2}, std::vector<int>{1})); }

        SUBCASE("unsupported integer critical label") {
            check_rejected(encode_header_items(2, std::uint64_t{99}, true, std::uint64_t{2}, std::vector<int>{99}));
        }

        SUBCASE("unsupported text critical label") {
            check_rejected(encode_header_items(2, std::string{"custom"}, true, std::uint64_t{2}, std::vector<std::string>{"custom"}));
        }
    }

    TEST_CASE("COSE truncated signing structures preserve the destination") {
        SUBCASE("truncated cose_sign1 preserves the destination") {
            const cose_sign1 source{
                .protected_header = byte_string{std::byte{0x41}},
                .unprotected      = header_map{.alg = std::nullopt, .kid = byte_string{std::byte{0x42}}, .crit = {}},
                .payload          = byte_string{std::byte{0x43}},
                .signature        = byte_string{std::byte{0x44}},
            };
            auto encoded = encode_to_bytes(source);
            REQUIRE(encoded);
            encoded->pop_back();

            cose_sign1 decoded{
                .protected_header = byte_string{std::byte{0x51}},
                .unprotected      = header_map{.alg = std::nullopt, .kid = byte_string{std::byte{0x52}}, .crit = {}},
                .payload          = byte_string{std::byte{0x53}},
                .signature        = byte_string{std::byte{0x54}},
            };
            auto result = make_decoder(*encoded)(decoded);
            REQUIRE_FALSE(result);
            CHECK_EQ(decoded.protected_header, byte_string{std::byte{0x51}});
            CHECK_EQ(decoded.unprotected.kid, byte_string{std::byte{0x52}});
            CHECK_EQ(decoded.payload, std::optional<byte_string>{byte_string{std::byte{0x53}}});
            CHECK_EQ(decoded.signature, byte_string{std::byte{0x54}});
        }

        SUBCASE("truncated sig_structure preserves the destination") {
            const sig_structure source{
                .context        = "Signature",
                .body_protected = byte_string{std::byte{0x91}},
                .sign_protected = byte_string{std::byte{0x92}},
                .external_aad   = byte_string{std::byte{0x93}},
                .payload        = byte_string{std::byte{0x94}},
            };
            auto encoded = encode_to_bytes(source);
            REQUIRE(encoded);
            encoded->pop_back();

            sig_structure decoded{
                .context        = "old",
                .body_protected = byte_string{std::byte{0xA1}},
                .sign_protected = byte_string{std::byte{0xA2}},
                .external_aad   = byte_string{std::byte{0xA3}},
                .payload        = byte_string{std::byte{0xA4}},
            };
            auto result = make_decoder(*encoded)(decoded);
            REQUIRE_FALSE(result);
            CHECK_EQ(decoded.context, "old");
            CHECK_EQ(decoded.body_protected, byte_string{std::byte{0xA1}});
            CHECK_EQ(decoded.sign_protected, std::optional<byte_string>{byte_string{std::byte{0xA2}}});
            CHECK_EQ(decoded.external_aad, byte_string{std::byte{0xA3}});
            CHECK_EQ(decoded.payload, byte_string{std::byte{0xA4}});
        }
    }

    TEST_CASE("COSE Sign rejects an empty wire signature array") {
        auto      input = to_bytes("8440a0410180");
        cose_sign decoded{
            .protected_header = byte_string{std::byte{0x11}},
            .unprotected      = header_map{.alg = std::nullopt, .kid = byte_string{std::byte{0x12}}, .crit = {}},
            .payload          = byte_string{std::byte{0x13}},
            .signatures       = {cose_signature{.protected_header = {}, .unprotected = {}, .signature = byte_string{std::byte{0x14}}}},
        };
        auto decode_result = make_decoder(input)(decoded);
        REQUIRE_FALSE(decode_result);
        CHECK_EQ(decode_result.error(), status_code::unexpected_group_size);
        CHECK_EQ(decoded.protected_header, byte_string{std::byte{0x11}});
        CHECK_EQ(decoded.unprotected.kid, byte_string{std::byte{0x12}});
        CHECK_EQ(decoded.payload, std::optional<byte_string>{byte_string{std::byte{0x13}}});
        REQUIRE_EQ(decoded.signatures.size(), 1U);
        CHECK_EQ(decoded.signatures.front().signature, byte_string{std::byte{0x14}});
    }

    TEST_CASE("encoded item view options decode non-empty CWT claims maps") {
        claims_set claims;
        claims.issuer     = "idp";
        claims.expiration = std::int64_t{42};

        std::vector<std::byte> encoded;
        auto                   enc = make_encoder(encoded);
        REQUIRE(enc(claims));

        claims_set decoded;
        auto       dec    = make_decoder_with_options<encoded_item_view_decoder_options>(encoded);
        auto       result = dec(decoded);

        REQUIRE(result);
        CHECK_EQ(decoded.issuer, claims.issuer);
        CHECK_EQ(decoded.expiration, claims.expiration);
        CHECK_EQ(to_hex(result->bytes()), to_hex(encoded));
        CHECK_EQ(dec.tell(), encoded.end());
    }

    TEST_CASE("CWT claims reject duplicate integer labels without replacing the destination") {
        constexpr std::array duplicate_maps{
            "a2016161016162", "a201616118016162", "a21863610118636162", "a220f520f4", "a23bfffffffffffffffff53bfffffffffffffffff4",
        };

        for (const auto hex : duplicate_maps) {
            CAPTURE(hex);
            auto       input = to_bytes(hex);
            claims_set decoded;
            decoded.issuer = "unchanged";
            auto result    = make_decoder(input)(decoded);

            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), status_code::error);
            CHECK_EQ(decoded.issuer, "unchanged");
        }
    }

    TEST_CASE("CWT claims decode definite and indefinite maps with atomic failures") {
        SUBCASE("indefinite registered claim") {
            auto       input = to_bytes("bf0163696470ff");
            claims_set decoded;
            auto       dec = make_decoder(input);

            REQUIRE(dec(decoded));
            CHECK_EQ(decoded.issuer, "idp");
            CHECK_EQ(dec.tell(), input.end());
        }

        SUBCASE("empty indefinite map") {
            auto       input = to_bytes("bfff");
            claims_set decoded;
            auto       dec = make_decoder(input);

            REQUIRE(dec(decoded));
            CHECK_FALSE(decoded.issuer);
            CHECK_EQ(dec.tell(), input.end());
        }

        SUBCASE("unknown claim with nested indefinite value") {
            auto       input = to_bytes("bf18819f0102ff0163696470ff");
            claims_set decoded;
            auto       dec = make_decoder(input);

            REQUIRE(dec(decoded));
            CHECK_EQ(decoded.issuer, "idp");
            CHECK_EQ(dec.tell(), input.end());
        }

        SUBCASE("non-contiguous input") {
            const auto            bytes = to_bytes("bf0163696470ff");
            std::deque<std::byte> input(bytes.begin(), bytes.end());
            claims_set            decoded;
            auto                  dec = make_decoder(input);

            REQUIRE(dec(decoded));
            CHECK_EQ(decoded.issuer, "idp");
            CHECK_EQ(dec.tell(), input.end());
        }

        SUBCASE("missing break") {
            auto       input = to_bytes("bf0163696470");
            claims_set decoded;
            decoded.issuer = "unchanged";
            auto result    = make_decoder(input)(decoded);

            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), status_code::incomplete);
            CHECK_EQ(decoded.issuer, "unchanged");
        }

        SUBCASE("break used as value") {
            auto       input = to_bytes("bf01ff");
            claims_set decoded;
            decoded.issuer = "unchanged";
            auto result    = make_decoder(input)(decoded);

            REQUIRE_FALSE(result);
            CHECK_EQ(decoded.issuer, "unchanged");
        }
    }

    TEST_CASE("encoded item view options retain complete indefinite CWT claims maps") {
        auto       input = to_bytes("bf016369647004182aff");
        claims_set decoded;
        auto       dec    = make_decoder_with_options<encoded_item_view_decoder_options>(input);
        auto       result = dec(decoded);

        REQUIRE(result);
        CHECK_EQ(decoded.issuer, "idp");
        CHECK_EQ(decoded.expiration, numeric_date{std::int64_t{42}});
        CHECK_EQ(to_hex(result->bytes()), to_hex(input));
        CHECK_EQ(dec.tell(), input.end());
    }

    TEST_CASE("CWT NumericDate rejects integers outside the int64 domain") {
        constexpr std::array accepted{
            std::pair{"a1041b7fffffffffffffff", std::numeric_limits<std::int64_t>::max()},
            std::pair{"a1043b7fffffffffffffff", std::numeric_limits<std::int64_t>::min()},
        };
        for (const auto &[hex, expected_value] : accepted) {
            CAPTURE(hex);
            auto       input = to_bytes(hex);
            claims_set decoded;

            REQUIRE(make_decoder(input)(decoded));
            CHECK_EQ(decoded.expiration, numeric_date{expected_value});
        }

        {
            auto       input = to_bytes("a104fb3ff8000000000000");
            claims_set decoded;
            REQUIRE(make_decoder(input)(decoded));
            CHECK_EQ(decoded.expiration, numeric_date{1.5});
        }

        constexpr std::array rejected{
            "a1041b8000000000000000",
            "a1041bffffffffffffffff",
            "a1043b8000000000000000",
            "a1043bffffffffffffffff",
        };
        for (const auto hex : rejected) {
            CAPTURE(hex);
            auto       input = to_bytes(hex);
            claims_set decoded;
            decoded.expiration = std::int64_t{42};
            auto result        = make_decoder(input)(decoded);

            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), status_code::no_match_for_int_on_buffer);
            CHECK_EQ(decoded.expiration, numeric_date{std::int64_t{42}});
        }
    }

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
        auto                   enc = make_encoder(encoded);
        REQUIRE(enc(as_cwt(as_cose_sign1(message))));

        CHECK(encoded.size() > message.signature.size());
        CHECK_EQ(to_hex(std::span<const std::byte>{encoded.data(), 8}), "d83dd28443a10126");
    }

    TEST_CASE("COSE headers decode indefinite maps and critical-label arrays") {
        SUBCASE("indefinite map") {
            auto       input = to_bytes("bf0126ff");
            header_map decoded;
            auto       dec = make_decoder(input);

            REQUIRE(dec(decoded));
            CHECK_EQ(decoded.alg, algorithm::es256);
            CHECK_EQ(dec.tell(), input.end());
        }

        SUBCASE("indefinite critical-label array") {
            auto       input = to_bytes("a3029f0104ff01260441aa");
            header_map decoded;

            REQUIRE(make_decoder(input)(decoded));
            CHECK_EQ(decoded.alg, algorithm::es256);
            CHECK_EQ(decoded.kid, byte_string{std::byte{0xaa}});
            CHECK_EQ(decoded.crit, (std::vector<header_label>{integer{1}, integer{4}}));
        }

        SUBCASE("duplicate label in indefinite map") {
            auto       input = to_bytes("bf01260126ff");
            header_map decoded;
            decoded.alg = algorithm::es384;
            auto result = make_decoder(input)(decoded);

            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), status_code::error);
            CHECK_EQ(decoded.alg, algorithm::es384);
        }

        SUBCASE("missing map break") {
            auto       input = to_bytes("bf0126");
            header_map decoded;
            auto       result = make_decoder(input)(decoded);

            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), status_code::incomplete);
        }
    }

    TEST_CASE("COSE protected headers consume unknown noncritical text labels") {
        auto encoded = encode_header_items(2, std::string{"custom"}, std::vector<int>{1, 2, 3}, std::uint64_t{1}, algorithm::es256);
        REQUIRE(encoded);

        auto decoded = decode_protected_header(*encoded);
        REQUIRE(decoded);
        CHECK_EQ(decoded->alg, algorithm::es256);
        CHECK_FALSE(decoded->kid);
        CHECK(decoded->crit.empty());
    }

    TEST_CASE("COSE protected headers consume the minimum negative noncritical label") {
        auto encoded = encode_header_items(2, integer{negative{0}}, true, std::uint64_t{1}, algorithm::es256);
        REQUIRE(encoded);

        auto decoded = decode_protected_header(*encoded);
        REQUIRE(decoded);
        CHECK_EQ(decoded->alg, algorithm::es256);
        CHECK_FALSE(decoded->kid);
        CHECK(decoded->crit.empty());
    }

    TEST_CASE("COSE protected headers reject malformed labels and values") {
        auto check_rejected = [](expected<byte_string, status_code> encoded) {
            REQUIRE(encoded);
            auto decoded = decode_protected_header(*encoded);
            REQUIRE_FALSE(decoded);
            CHECK_EQ(decoded.error(), status_code::error);
        };

        SUBCASE("duplicate registered label") {
            check_rejected(encode_header_items(2, std::uint64_t{1}, algorithm::es256, std::uint64_t{1}, algorithm::es256));
        }

        SUBCASE("duplicate text label") {
            check_rejected(encode_header_items(2, std::string{"custom"}, true, std::string{"custom"}, false));
        }

        SUBCASE("wide unsigned algorithm") {
            check_rejected(encode_header_items(1, std::uint64_t{1}, std::numeric_limits<std::uint64_t>::max()));
        }

        SUBCASE("trailing protected header item") {
            auto encoded = encode_protected_header(header_map{.alg = algorithm::es256, .kid = std::nullopt, .crit = {}});
            REQUIRE(encoded);
            encoded->push_back(std::byte{0x00});
            check_rejected(std::move(encoded));
        }
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
        auto                   enc = make_encoder(encoded);
        REQUIRE(enc(as_cwt(as_cose_sign(message))));

        CHECK(encoded.size() > signature.signature.size());
        CHECK_EQ(to_hex(std::span<const std::byte>{encoded.data(), 9}), "d83dd8628443a10126");
    }

    TEST_CASE("COSE envelope arrays consume exactly their fields and closing delimiter") {
        const auto check = []<typename Envelope>(const std::string &wire, auto inspect) {
            const auto bytes       = to_bytes(wire + "07");
            const auto check_input = [&](const auto &input) {
                Envelope value;
                auto     dec = make_decoder(input);
                REQUIRE(dec(value));
                inspect(value);
                int following{};
                REQUIRE(dec(following));
                CHECK_EQ(following, 7);
                CHECK(dec.tell() == std::ranges::end(input));
            };
            check_input(bytes);
            const std::deque<std::byte> segmented(bytes.begin(), bytes.end());
            check_input(segmented);
            const std::list<std::byte> linked(bytes.begin(), bytes.end());
            const auto                 unsized = std::ranges::subrange(linked.begin(), linked.end());
            static_assert(!std::ranges::sized_range<decltype(unsized)>);
            check_input(unsized);
        };
        for (const auto *wire : {"8340a04162", "9f40a04162ff"}) {
            CAPTURE(wire);
            check.template operator()<cose_signature>(wire, [](const auto &value) {
                CHECK(value.protected_header.empty());
                CHECK(value.unprotected.empty());
                CHECK(value.signature == byte_string{std::byte{'b'}});
            });
        }
        for (const auto *wire : {"8440a041614162", "9f40a041614162ff"}) {
            CAPTURE(wire);
            check.template operator()<cose_sign1>(wire, [](const auto &value) {
                CHECK(value.protected_header.empty());
                CHECK(value.unprotected.empty());
                REQUIRE(value.payload);
                CHECK(*value.payload == byte_string{std::byte{'a'}});
                CHECK(value.signature == byte_string{std::byte{'b'}});
            });
        }
        for (const bool indefinite_outer : {false, true}) {
            for (const bool indefinite_signatures : {false, true}) {
                for (const auto *signature : {"8340a04162", "9f40a04162ff"}) {
                    const auto wire = std::string(indefinite_outer ? "9f" : "84") + "40a04161" + (indefinite_signatures ? "9f" : "81") +
                                      signature + (indefinite_signatures ? "ff" : "") + (indefinite_outer ? "ff" : "");
                    CAPTURE(wire);
                    check.template operator()<cose_sign>(wire, [](const auto &value) {
                        CHECK(value.protected_header.empty());
                        CHECK(value.unprotected.empty());
                        REQUIRE(value.payload);
                        CHECK(*value.payload == byte_string{std::byte{'a'}});
                        REQUIRE_EQ(value.signatures.size(), 1U);
                        CHECK(value.signatures.front().protected_header.empty());
                        CHECK(value.signatures.front().unprotected.empty());
                        CHECK(value.signatures.front().signature == byte_string{std::byte{'b'}});
                    });
                }
            }
        }
    }

    TEST_CASE("COSE envelope arrays reject missing excess or malformed members atomically") {
        struct fixture {
            const char *wire;
            status_code status;
        };
        const auto check = []<typename Envelope>(const fixture &test) {
            CAPTURE(test.wire);
            Envelope value;
            value.protected_header = byte_string{std::byte{'p'}};
            value.unprotected.kid  = byte_string{std::byte{'k'}};
            if constexpr (std::same_as<Envelope, cose_sign>) {
                value.signatures = {cose_signature{.protected_header = {}, .unprotected = {}, .signature = byte_string{std::byte{'s'}}}};
            } else {
                value.signature = byte_string{std::byte{'s'}};
            }
            if constexpr (!std::same_as<Envelope, cose_signature>) {
                value.payload = byte_string{std::byte{'v'}};
            }
            const auto bytes  = to_bytes(test.wire);
            const auto result = make_decoder(bytes)(value);
            REQUIRE_FALSE(result);
            CHECK(result.error() == test.status);
            CHECK(value.protected_header == byte_string{std::byte{'p'}});
            CHECK(value.unprotected.kid == std::optional<byte_string>{byte_string{std::byte{'k'}}});
            if constexpr (std::same_as<Envelope, cose_sign>) {
                REQUIRE_EQ(value.signatures.size(), 1U);
                CHECK(value.signatures.front().signature == byte_string{std::byte{'s'}});
            } else {
                CHECK(value.signature == byte_string{std::byte{'s'}});
            }
            if constexpr (!std::same_as<Envelope, cose_signature>) {
                CHECK(value.payload == std::optional<byte_string>{byte_string{std::byte{'v'}}});
            }
        };
        for (const auto &test :
             std::array{fixture{"82", status_code::unexpected_group_size}, fixture{"84", status_code::unexpected_group_size},
                        fixture{"9fff", status_code::unexpected_group_size}, fixture{"9f40ff", status_code::unexpected_group_size},
                        fixture{"9f40a0ff", status_code::unexpected_group_size}, fixture{"9f40a040", status_code::incomplete},
                        fixture{"9f40a04000ff", status_code::unexpected_group_size}, fixture{"9f40a041", status_code::incomplete},
                        fixture{"8301a04162", status_code::no_match_for_bstr_on_buffer},
                        fixture{"9f01a04162ff", status_code::no_match_for_bstr_on_buffer},
                        fixture{"a0", status_code::no_match_for_array_on_buffer}}) {
            check.template operator()<cose_signature>(test);
        }
        for (const auto &test :
             std::array{fixture{"83", status_code::unexpected_group_size}, fixture{"85", status_code::unexpected_group_size},
                        fixture{"9fff", status_code::unexpected_group_size}, fixture{"9f40a040ff", status_code::unexpected_group_size},
                        fixture{"9f40a04040", status_code::incomplete}, fixture{"9f40a0404000ff", status_code::unexpected_group_size},
                        fixture{"9f40a04041", status_code::incomplete}, fixture{"a0", status_code::no_match_for_array_on_buffer}}) {
            check.template operator()<cose_sign1>(test);
        }
        for (const auto &test :
             std::array{fixture{"83", status_code::unexpected_group_size}, fixture{"85", status_code::unexpected_group_size},
                        fixture{"9f40a040ff", status_code::unexpected_group_size}, fixture{"9f40a040818340a040", status_code::incomplete},
                        fixture{"9f40a040818340a04000ff", status_code::unexpected_group_size},
                        fixture{"9f40a040819f40a0ffff", status_code::unexpected_group_size},
                        fixture{"9f40a040819f40a04000ffff", status_code::unexpected_group_size},
                        fixture{"9f40a04080ff", status_code::unexpected_group_size}}) {
            check.template operator()<cose_sign>(test);
        }
    }

    TEST_CASE("tagged indefinite COSE envelopes retain exact raw item views") {
        const auto bytes = to_bytes("d83dd29f40a041614162ff07");
        cose_sign1 value;
        auto       tagged = make_tag_pair(cose_sign1_tag{}, value);
        auto       cwt    = make_tag_pair(cwt_tag{}, tagged);
        auto       dec    = make_decoder_with_options<encoded_item_view_decoder_options>(bytes);
        const auto result = dec(cwt);
        REQUIRE(result);
        CHECK_EQ(to_hex(result->bytes()), "d83dd29f40a041614162ff");
        REQUIRE(value.payload);
        CHECK(*value.payload == byte_string{std::byte{'a'}});
        CHECK(value.signature == byte_string{std::byte{'b'}});
        int following{};
        REQUIRE(dec(following));
        CHECK_EQ(following, 7);
    }

#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
    TEST_CASE("indefinite COSE framing preserves a real ES256 signature") {
        auto              key = make_p256_key();
        const byte_string payload{std::byte{'a'}, std::byte{'b'}};
        auto              source = sign1<crypto_es256_backend>(key.get(), {}, {}, payload);
        REQUIRE(source);
        const auto before = make_sign1_tbs(*source);
        REQUIRE(before);
        auto encoded = encode_to_bytes(*source);
        REQUIRE(encoded);
        REQUIRE(encoded->front() == std::byte{0x84});
        encoded->front() = std::byte{0x9f};
        encoded->push_back(std::byte{0xff});
        cose_sign1 decoded;
        REQUIRE(make_decoder(*encoded)(decoded));
        CHECK(decoded.payload == source->payload);
        CHECK(decoded.signature == source->signature);
        const auto after = make_sign1_tbs(decoded);
        REQUIRE(after);
        CHECK(*before == *after);
        REQUIRE(verify_sign1<crypto_es256_backend>(key.get(), decoded));
    }
#endif

    TEST_CASE("CWT claims consume unknown text claim keys") {
        SUBCASE("definite text label") {
            std::vector<std::byte> encoded;
            auto                   enc = make_encoder(encoded);
            REQUIRE(enc(as_map{2}, std::string{"private"}, std::vector<int>{1, 2, 3}, std::uint64_t{1}, std::string{"issuer"}));

            claims_set decoded;
            auto       dec = make_decoder(encoded);
            REQUIRE(dec(decoded));
            CHECK_EQ(decoded.issuer, "issuer");
            CHECK_FALSE(decoded.subject);
            CHECK_FALSE(decoded.audience);
            CHECK_EQ(dec.tell(), encoded.end());
        }

        SUBCASE("indefinite text label") {
            auto       input = to_bytes("a27f637072696476617465ff830102030166697373756572");
            claims_set decoded;
            auto       dec = make_decoder(input);

            REQUIRE(dec(decoded));
            CHECK_EQ(decoded.issuer, "issuer");
            CHECK_EQ(dec.tell(), input.end());
        }

        SUBCASE("indefinite map, label, and value") {
            auto       input = to_bytes("bf7f637072696476617465ff9f0102ff0163696470ff");
            claims_set decoded;
            auto       dec = make_decoder(input);

            REQUIRE(dec(decoded));
            CHECK_EQ(decoded.issuer, "idp");
            CHECK_EQ(dec.tell(), input.end());
        }

        SUBCASE("non-contiguous input") {
            const auto            bytes = to_bytes("a27f637072696476617465ff830102030166697373756572");
            std::deque<std::byte> input(bytes.begin(), bytes.end());
            claims_set            decoded;
            auto                  dec = make_decoder(input);

            REQUIRE(dec(decoded));
            CHECK_EQ(decoded.issuer, "issuer");
            CHECK_EQ(dec.tell(), input.end());
        }

        SUBCASE("integer and text labels remain distinct") {
            auto       input = to_bytes("a20166697373756572613101");
            claims_set decoded;

            REQUIRE(make_decoder(input)(decoded));
            CHECK_EQ(decoded.issuer, "issuer");
        }
    }

    TEST_CASE("CWT claims reject duplicate text labels without replacing the destination") {
        constexpr std::array duplicate_maps{
            "a2677072697661746501677072697661746502",
            "a27f637072696476617465ff01677072697661746502",
        };

        for (const auto hex : duplicate_maps) {
            CAPTURE(hex);
            auto       input = to_bytes(hex);
            claims_set decoded;
            decoded.issuer = "unchanged";
            auto result    = make_decoder(input)(decoded);

            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), status_code::error);
            CHECK_EQ(decoded.issuer, "unchanged");
        }
    }

    TEST_CASE("CWT claims decode many unique unknown text labels") {
        constexpr std::uint64_t label_count = 256;

        std::vector<std::byte> encoded;
        auto                   enc = make_encoder(encoded);
        REQUIRE(enc(as_map{label_count + 1U}));
        for (std::uint64_t index = 0; index < label_count; ++index) {
            auto       label      = std::string{"private-"} + std::string(128U, 'x');
            const auto index_text = std::to_string(index);
            label.append(3U - index_text.size(), '0');
            label += index_text;
            REQUIRE(enc(label, index));
        }
        REQUIRE(enc(std::uint64_t{1}, std::string{"issuer"}));

        claims_set decoded;
        auto       dec = make_decoder(encoded);

        REQUIRE(dec(decoded));
        CHECK_EQ(decoded.issuer, "issuer");
        CHECK_EQ(dec.tell(), encoded.end());
    }

    TEST_CASE("CWT claims reject truncated indefinite text labels atomically") {
        auto       input = to_bytes("a17f63707269");
        claims_set decoded;
        decoded.issuer = "unchanged";
        auto result    = make_decoder(input)(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
        CHECK_EQ(decoded.issuer, "unchanged");
    }

    TEST_CASE("encoded item view options retain CWT maps with text labels") {
        auto       input = to_bytes("a27f637072696476617465ff830102030166697373756572");
        claims_set decoded;
        auto       dec    = make_decoder_with_options<encoded_item_view_decoder_options>(input);
        auto       result = dec(decoded);

        REQUIRE(result);
        CHECK_EQ(decoded.issuer, "issuer");
        CHECK_EQ(to_hex(result->bytes()), to_hex(input));
        CHECK_EQ(dec.tell(), input.end());
    }

} // TEST_SUITE("cbor_wire/cwt")
