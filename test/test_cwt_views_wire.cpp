#include "cwt_test_support.h"
#include "test_util.h"

TEST_SUITE("cbor_wire/cwt") {
    TEST_CASE("cwt claims views borrow exact payload ranges and consume one item") {
        const auto  bytes = to_bytes("a3016269640261750742010209");
        claims_view claims;
        auto        dec    = make_decoder_with_options<encoded_item_view_decoder_options, codec::cwt>(bytes);
        const auto  result = dec(claims);
        REQUIRE(result);
        REQUIRE(claims.issuer);
        REQUIRE(claims.subject);
        REQUIRE(claims.cwt_id);
        CHECK(claims.issuer->data() == reinterpret_cast<const char *>(bytes.data() + 3));
        CHECK(claims.subject->data() == reinterpret_cast<const char *>(bytes.data() + 7));
        CHECK(claims.cwt_id->data() == bytes.data() + 10);
        CHECK(claims.issuer->size() == 2);
        CHECK(claims.cwt_id->size() == 2);
        CHECK(to_hex(result->bytes()) == "a30162696402617507420102");
        const auto encoded = encode_to_bytes(claims);
        REQUIRE(encoded);
        CHECK(to_hex(*encoded) == "a30162696402617507420102");
        int following{};
        REQUIRE(dec(following));
        CHECK(following == 9);
        CHECK(dec.tell() == bytes.end());
    }

    TEST_CASE("borrowed cose envelopes accept indefinite framing and definite fields") {
        for (const auto *wire : {"8441a0a104416b41614162", "9f41a0bf04416bff41614162ff"}) {
            CAPTURE(wire);
            const auto      bytes = to_bytes(std::string{wire} + "09");
            cose_sign1_view view;
            auto            dec = make_decoder<codec::cwt>(bytes);
            REQUIRE(dec(view));
            CHECK(to_hex(view.protected_header) == "a0");
            REQUIRE(view.unprotected.kid);
            CHECK(to_hex(*view.unprotected.kid) == "6b");
            REQUIRE(view.payload);
            CHECK(to_hex(*view.payload) == "61");
            CHECK(to_hex(view.signature) == "62");
            CHECK(view.protected_header.data() == bytes.data() + 2);
            CHECK(view.signature.data() == bytes.data() + bytes.size() - (wire[0] == '9' ? 3 : 2));
            int following{};
            REQUIRE(dec(following));
            CHECK(following == 9);
            CHECK(dec.tell() == bytes.end());
        }
    }

    TEST_CASE("borrowed cwt decoding rejects fragmented fields and noncontiguous input") {
        SUBCASE("indefinite text remains available through the owning model") {
            const auto  bytes = to_bytes("a1017f61616162ff");
            claims_view view{.issuer     = "retained",
                             .subject    = std::nullopt,
                             .audience   = std::nullopt,
                             .expiration = std::nullopt,
                             .not_before = std::nullopt,
                             .issued_at  = std::nullopt,
                             .cwt_id     = std::nullopt};
            auto        dec    = make_decoder<codec::cwt>(bytes);
            const auto  result = dec(view);
            REQUIRE_FALSE(result);
            CHECK(result.error() == status_code::no_match_for_tstr_on_buffer);
            CHECK(view.issuer == "retained");
            CHECK(dec.tell() == bytes.begin() + 3);
            claims_set owned;
            REQUIRE(make_decoder<codec::cwt>(bytes)(owned));
            CHECK(owned.issuer == "ab");
        }
        SUBCASE("indefinite bytes remain available through the owning model") {
            const auto      bytes = to_bytes("8440a05f41614162ff40");
            cose_sign1_view view;
            const auto      result = make_decoder<codec::cwt>(bytes)(view);
            REQUIRE_FALSE(result);
            CHECK(result.error() == status_code::no_match_for_bstr_on_buffer);
            cose_sign1 owned;
            REQUIRE(make_decoder<codec::cwt>(bytes)(owned));
            REQUIRE(owned.payload);
            CHECK(to_hex(*owned.payload) == "6162");
        }
        SUBCASE("noncontiguous input reports the view boundary after its initial byte") {
            const auto                  bytes = to_bytes("a1016161");
            const std::deque<std::byte> chunks(bytes.begin(), bytes.end());
            claims_view                 view;
            auto                        dec    = make_decoder<codec::cwt>(chunks);
            const auto                  result = dec(view);
            REQUIRE_FALSE(result);
            CHECK(result.error() == status_code::contiguous_view_on_non_contiguous_data);
            CHECK(dec.tell() == chunks.begin() + 1);
            claims_set owned;
            REQUIRE(make_decoder<codec::cwt>(chunks)(owned));
            CHECK(owned.issuer == "a");
        }
    }

    TEST_CASE("borrowed cwt failures preserve the destination and successful decode clears it") {
        const auto bytes = to_bytes("a201616107420102");
        for (std::size_t length = 0; length < bytes.size(); ++length) {
            CAPTURE(length);
            const std::span<const std::byte> prefix{bytes.data(), length};
            claims_view                      view{.issuer     = "retained",
                                                  .subject    = "stale",
                                                  .audience   = std::nullopt,
                                                  .expiration = std::nullopt,
                                                  .not_before = std::nullopt,
                                                  .issued_at  = std::nullopt,
                                                  .cwt_id     = std::nullopt};
            const auto                       result = make_decoder<codec::cwt>(prefix)(view);
            REQUIRE_FALSE(result);
            CHECK(result.error() == status_code::incomplete);
            CHECK(view.issuer == "retained");
            CHECK(view.subject == "stale");
        }
        claims_view view{.issuer     = "retained",
                         .subject    = "stale",
                         .audience   = std::nullopt,
                         .expiration = std::nullopt,
                         .not_before = std::nullopt,
                         .issued_at  = std::nullopt,
                         .cwt_id     = std::nullopt};
        REQUIRE(make_decoder<codec::cwt>(bytes)(view));
        CHECK(view.issuer == "a");
        CHECK_FALSE(view.subject);
        REQUIRE(view.cwt_id);
        const auto empty = to_bytes("bfff");
        REQUIRE(make_decoder<codec::cwt>(empty)(view));
        CHECK_FALSE(view.issuer);
        CHECK_FALSE(view.cwt_id);

        const auto malformed = to_bytes("a2016161016162");
        view.issuer          = "unchanged";
        const auto result    = make_decoder<codec::cwt>(malformed)(view);
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::error);
        CHECK(view.issuer == "unchanged");
    }
    TEST_CASE("cwt views skip text claim labels and reject their duplicates") {
        const auto  bytes = to_bytes("bf7f637072696476617465ff9f0102ff0163696470ff09");
        claims_view view;
        auto        dec    = make_decoder_with_options<encoded_item_view_decoder_options, codec::cwt>(bytes);
        const auto  result = dec(view);
        REQUIRE(result);
        REQUIRE(view.issuer);
        CHECK(*view.issuer == "idp");
        CHECK(view.issuer->data() == reinterpret_cast<const char *>(bytes.data() + 18));
        CHECK(to_hex(result->bytes()) == "bf7f637072696476617465ff9f0102ff0163696470ff");
        int following{};
        REQUIRE(dec(following));
        CHECK(following == 9);
        CHECK(dec.tell() == bytes.end());

        const auto duplicate = to_bytes("a27f637072696476617465ff01677072697661746502");
        const auto rejected  = make_decoder<codec::cwt>(duplicate)(view);
        REQUIRE_FALSE(rejected);
        CHECK(rejected.error() == status_code::error);
        CHECK(view.issuer == "idp");
        const auto truncated  = to_bytes("a17f63707269");
        const auto incomplete = make_decoder<codec::cwt>(truncated)(view);
        REQUIRE_FALSE(incomplete);
        CHECK(incomplete.error() == status_code::incomplete);
        CHECK(view.issuer == "idp");
    }
}
