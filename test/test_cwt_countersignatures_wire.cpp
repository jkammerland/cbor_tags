#include "cwt_openssl_test_support.h"

#include <cbor_tags/cwt/countersignatures.h>
#include <list>

TEST_SUITE("cbor_wire/cwt/countersignatures") {
    TEST_CASE("counter signature input includes independent framing and original signature") {
        cose_sign1       target{.protected_header = {std::byte{0xa0}},
                                .unprotected      = {},
                                .payload          = std::vector<std::byte>{std::byte{0x01}},
                                .signature        = {std::byte{0x02}}};
        cose_signature   counter{.protected_header = {std::byte{0xa0}}, .unprotected = {}, .signature = {}};
        const std::array aad{std::byte{0x03}};
        auto             input = make_countersign_tbs(target, counter, aad);
        REQUIRE(input);

        // [context, target protected, counter protected, AAD, payload, [signature]].
        std::vector<std::byte> expected{std::byte{0x86}, std::byte{0x72}};
        for (char c : std::string_view{"CounterSignatureV2"}) {
            expected.push_back(std::byte(c));
        }
        const std::vector<std::byte> suffix{std::byte{0x41}, std::byte{0xa0}, std::byte{0x41}, std::byte{0xa0},
                                            std::byte{0x41}, std::byte{0x03}, std::byte{0x41}, std::byte{0x01},
                                            std::byte{0x81}, std::byte{0x41}, std::byte{0x02}};
        expected.insert(expected.end(), suffix.begin(), suffix.end());
        CHECK(input->flatten() == expected);

        bool original_signature_borrowed = false;
        for (const auto &part : *input) {
            original_signature_borrowed |= part.is_borrowed() && part.bytes().data() == target.signature.data();
        }
        CHECK(original_signature_borrowed);
    }

    TEST_CASE("signature target uses the original context and no additional fields") {
        cose_signature target{.protected_header = {}, .unprotected = {}, .signature = {std::byte{0x02}}};
        auto           input = make_countersign0_tbs(target);
        REQUIRE(input);

        std::vector<std::byte> expected{std::byte{0x84}, std::byte{0x71}};
        for (char c : std::string_view{"CounterSignature0"}) {
            expected.push_back(std::byte(c));
        }
        expected.insert(expected.end(), {std::byte{0x40}, std::byte{0x40}, std::byte{0x41}, std::byte{0x02}});
        CHECK(input->flatten() == expected);
    }

    TEST_CASE("full counter collections accept singleton and list and reject empty or malformed arrays") {
        const std::vector<std::vector<std::byte>> valid{
            {std::byte{0x83}, std::byte{0x40}, std::byte{0xa0}, std::byte{0x41}, std::byte{0x01}},
            {std::byte{0x81}, std::byte{0x83}, std::byte{0x40}, std::byte{0xa0}, std::byte{0x41}, std::byte{0x01}},
            {std::byte{0x9f}, std::byte{0x40}, std::byte{0xa0}, std::byte{0x41}, std::byte{0x01}, std::byte{0xff}}};
        for (auto bytes : valid) {
            counter_collection<cose_signature> result;
            REQUIRE(make_decoder<codec::cwt>(bytes)(result));
            REQUIRE(result.signatures.size() == 1);
            CHECK(result.signatures.front().signature == std::vector<std::byte>{std::byte{1}});
        }

        const std::vector<std::vector<std::byte>> invalid{
            {std::byte{0x80}},
            {std::byte{0x9f}, std::byte{0xff}},
            {std::byte{0x82}, std::byte{0x40}, std::byte{0xa0}},
            {std::byte{0x83}, std::byte{0x40}, std::byte{0xa0}},
            {std::byte{0x84}, std::byte{0x40}, std::byte{0xa0}, std::byte{0x40}, std::byte{0x00}}};
        for (auto bytes : invalid) {
            counter_collection<cose_signature> result;
            CHECK_FALSE(make_decoder<codec::cwt>(bytes)(result));
        }

        counter_collection<cose_signature, true> legacy;
        auto                                     bytes = valid.front();
        REQUIRE(make_decoder<codec::cwt>(bytes)(legacy));
        CHECK_FALSE(encode_to_bytes(legacy));
        CHECK_FALSE(encode_to_bytes(counter_collection<cose_signature>{}));
    }

    TEST_CASE("counter collections decode once from unsized noncontiguous input") {
        std::list<std::byte> bytes{std::byte{0x83}, std::byte{0x40}, std::byte{0xa0}, std::byte{0x41}, std::byte{0x01}};
        std::ranges::subrange<decltype(bytes.cbegin()), decltype(bytes.cend()), std::ranges::subrange_kind::unsized> input{bytes.cbegin(),
                                                                                                                           bytes.cend()};
        static_assert(!std::ranges::sized_range<decltype(input)>);
        counter_collection<cose_signature> owned;
        auto                               decoder = make_decoder<codec::cwt>(input);
        REQUIRE(decoder(owned));
        CHECK(decoder.tell() == bytes.end());
        CHECK(owned.signatures.front().signature == std::vector<std::byte>{std::byte{1}});

        counter_collection<cose_signature_view> borrowed;
        auto                                    unavailable = make_decoder<codec::cwt>(input)(borrowed);
        REQUIRE_FALSE(unavailable);
        CHECK(unavailable.error() == status_code::contiguous_view_on_non_contiguous_data);
    }

    TEST_CASE("direct counter dispatch returns incomplete for a truncated byte-string length") {
        std::vector<std::byte>             bytes{std::byte{0x83}, std::byte{0x58}};
        counter_collection<cose_signature> result;
        auto                               decoder = make_decoder<codec::cwt>(bytes);
        auto                               status  = status_code::success;
        CHECK_NOTHROW(status = decoder.decode(result));
        CHECK(status == status_code::incomplete);
        CHECK(decoder.tell() == bytes.end());
        CHECK(result.signatures.empty());
    }

    TEST_CASE("standalone countersignature uses tag nineteen") {
        auto wrapped = as_countersignature(cose_signature{});
        auto bytes   = encode_to_bytes(wrapped);
        REQUIRE(bytes);
        const std::vector<std::byte> expected{std::byte{0xd3}, std::byte{0x83}, std::byte{0x40}, std::byte{0xa0}, std::byte{0x40}};
        CHECK(*bytes == expected);
    }
#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
    TEST_CASE("legacy full verification is explicit and preserves its historical sign1 coverage") {
        auto           key = cwt_test::make_p256_key();
        cose_sign1     target{.protected_header = {},
                              .unprotected      = {},
                              .payload          = std::vector<std::byte>{std::byte{1}},
                              .signature        = {std::byte{2}}};
        cose_signature counter{.protected_header = {std::byte{0xa1}, std::byte{1}, std::byte{0x28}}, .unprotected = {}, .signature = {}};

        std::vector<std::byte> input{std::byte{0x85}, std::byte{0x70}};
        for (char c : std::string_view{"CounterSignature"}) {
            input.push_back(std::byte(c));
        }
        input.insert(input.end(), {std::byte{0x40}, std::byte{0x43}, std::byte{0xa1}, std::byte{1}, std::byte{0x28}, std::byte{0x40},
                                   std::byte{0x41}, std::byte{1}});
        const std::array parts{std::span<const std::byte>{input}};
        auto             signature = openssl_esp256_backend::sign(key.get(), parts);
        REQUIRE(signature);
        counter.signature = *signature;

        REQUIRE(verify_legacy_countersignature<openssl_esp256_backend>(key.get(), target, counter));
        CHECK_FALSE(verify_countersignature<openssl_esp256_backend>(key.get(), target, counter));
        target.signature.front() ^= std::byte{1};
        CHECK(verify_legacy_countersignature<openssl_esp256_backend>(key.get(), target, counter));
    }
#endif
}
