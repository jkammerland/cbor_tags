#include "cwt_test_support.h"

#include <algorithm>
#include <cbor_tags/codec/unique_ptr.h>

namespace {
template <typename T>
concept CanProject = requires(T &&value) { as_view(std::forward<T>(value)); };
static_assert(CanProject<claims_set &> && CanProject<const claims_set &>);
static_assert(!CanProject<claims_set> && !CanProject<const claims_set>);
static_assert(!CanProject<header_map> && !CanProject<cose_sign1> && !CanProject<cose_sign>);
static_assert(!CanProject<cose_signature> && !CanProject<sig_structure>);
static_assert(std::is_aggregate_v<claims_set> && std::is_aggregate_v<claims_view>);

template <typename View, typename Owned, typename Inspect> void through_view(const Owned &source, Inspect inspect) {
    const auto bytes = encode_to_bytes(source);
    REQUIRE(bytes);
    View borrowed{};
    REQUIRE(make_decoder<codec::cwt>(*bytes)(borrowed));
    inspect(borrowed);
    const auto copied = encode_to_bytes(borrowed);
    REQUIRE(copied);
    Owned decoded{};
    REQUIRE(make_decoder<codec::cwt>(*copied)(decoded));
    inspect(decoded);
}
} // namespace

TEST_SUITE("roundtrip/cwt") {
    TEST_CASE("cwt claims views roundtrip all registered fields") {
        const claims_set source{.issuer     = "issuer",
                                .subject    = "subject",
                                .audience   = "audience",
                                .expiration = std::int64_t{42},
                                .not_before = 1.5,
                                .issued_at  = std::int64_t{-7},
                                .cwt_id     = byte_string{std::byte{9}}};
        through_view<claims_view>(source, [&](const auto &decoded) {
            CHECK(decoded.issuer == source.issuer);
            CHECK(decoded.subject == source.subject);
            REQUIRE(decoded.audience);
            CHECK(std::get<0>(*decoded.audience) == std::get<0>(*source.audience));
            CHECK(decoded.expiration == source.expiration);
            CHECK(decoded.not_before == source.not_before);
            CHECK(decoded.issued_at == source.issued_at);
            REQUIRE(decoded.cwt_id);
            CHECK(std::ranges::equal(*decoded.cwt_id, *source.cwt_id));
        });
        auto view = as_view(source);
        CHECK(view.issuer->data() == source.issuer->data());
        CHECK(view.subject->data() == source.subject->data());
        CHECK(std::get<0>(*view.audience).data() == std::get<0>(*source.audience).data());
        CHECK(view.cwt_id->data() == source.cwt_id->data());
    }

    TEST_CASE("cwt audience views preserve array shape and borrow each string") {
        claims_set source;
        source.audience       = std::vector<std::string>{"first", "second"};
        const auto projection = as_view(source);
        REQUIRE(projection.audience);
        const auto &projected = std::get<std::vector<std::string_view>>(*projection.audience);
        const auto &original  = std::get<std::vector<std::string>>(*source.audience);
        REQUIRE(projected.size() == 2);
        CHECK(projected[0].data() == original[0].data());
        CHECK(projected[1].data() == original[1].data());
        const auto bytes = encode_to_bytes(projection);
        REQUIRE(bytes);
        claims_view decoded;
        REQUIRE(make_decoder<codec::cwt>(*bytes)(decoded));
        REQUIRE(decoded.audience);
        CHECK(std::get<std::vector<std::string_view>>(*decoded.audience) == projected);
        const auto reencoded = encode_to_bytes(decoded);
        REQUIRE(reencoded);
        claims_set restored;
        REQUIRE(make_decoder<codec::cwt>(*reencoded)(restored));
        CHECK(restored.audience == source.audience);

        source.audience  = std::vector<std::string>{};
        const auto empty = as_view(source);
        REQUIRE(empty.audience);
        CHECK(std::get<std::vector<std::string_view>>(*empty.audience).empty());
        const auto empty_bytes = encode_to_bytes(empty);
        REQUIRE(empty_bytes);
        REQUIRE(make_decoder<codec::cwt>(*empty_bytes)(decoded));
        REQUIRE(decoded.audience);
        CHECK(std::get<std::vector<std::string_view>>(*decoded.audience).empty());
        source.audience.reset();
        CHECK_FALSE(as_view(source).audience);
    }

    TEST_CASE("application data projects into cwt views") {
        struct application_token {
            struct identity {
                std::string provider;
                std::string user;
            } identity;
            std::array<std::byte, 2> nonce;
            std::int64_t             deadline;
        };
        const application_token token{{"provider", "alice"}, {std::byte{1}, std::byte{2}}, 123};
        const claims_view       view{.issuer     = token.identity.provider,
                                     .subject    = token.identity.user,
                                     .audience   = std::nullopt,
                                     .expiration = token.deadline,
                                     .not_before = std::nullopt,
                                     .issued_at  = std::nullopt,
                                     .cwt_id     = token.nonce};
        const auto              encoded = encode_to_bytes(view);
        REQUIRE(encoded);
        claims_set decoded;
        REQUIRE(make_decoder<codec::cwt>(*encoded)(decoded));
        CHECK(decoded.issuer == token.identity.provider);
        CHECK(decoded.subject == token.identity.user);
        CHECK(decoded.expiration == numeric_date{token.deadline});
        REQUIRE(decoded.cwt_id);
        CHECK(std::ranges::equal(*decoded.cwt_id, token.nonce));
    }

    TEST_CASE("cose headers and envelopes roundtrip through borrowed models") {
        const header_map header{.alg = algorithm::es256, .kid = byte_string{std::byte{7}}, .crit = {integer{1}, integer{4}}};
        through_view<header_map_view>(header, [&](const auto &decoded) {
            CHECK(decoded.alg == header.alg);
            REQUIRE(decoded.kid);
            CHECK(std::ranges::equal(*decoded.kid, *header.kid));
            REQUIRE(decoded.crit.size() == 2);
            CHECK(std::get<integer>(decoded.crit[0]) == integer{1});
            CHECK(std::get<integer>(decoded.crit[1]) == integer{4});
        });
        const cose_signature signature{.protected_header = {std::byte{5}}, .unprotected = header, .signature = {std::byte{6}}};
        through_view<cose_signature_view>(signature, [&](const auto &decoded) {
            CHECK(std::ranges::equal(decoded.protected_header, signature.protected_header));
            CHECK(std::ranges::equal(decoded.signature, signature.signature));
            CHECK(decoded.unprotected.alg == header.alg);
        });
        const cose_sign1 single{.protected_header = signature.protected_header,
                                .unprotected      = header,
                                .payload          = byte_string{std::byte{8}},
                                .signature        = signature.signature};
        through_view<cose_sign1_view>(single, [&](const auto &decoded) {
            CHECK(std::ranges::equal(decoded.protected_header, single.protected_header));
            CHECK(std::ranges::equal(decoded.signature, single.signature));
            REQUIRE(decoded.payload);
            CHECK(std::ranges::equal(*decoded.payload, *single.payload));
        });
        const cose_sign multiple{.protected_header = single.protected_header,
                                 .unprotected      = header,
                                 .payload          = single.payload,
                                 .signatures       = {signature, signature}};
        through_view<cose_sign_view>(multiple, [&](const auto &decoded) {
            CHECK(std::ranges::equal(decoded.protected_header, multiple.protected_header));
            REQUIRE(decoded.payload);
            CHECK(std::ranges::equal(*decoded.payload, *multiple.payload));
            REQUIRE(decoded.signatures.size() == 2);
            for (const auto &item : decoded.signatures)
                CHECK(std::ranges::equal(item.signature, signature.signature));
        });
        const auto borrowed = as_view(multiple);
        CHECK(borrowed.protected_header.data() == multiple.protected_header.data());
        CHECK(borrowed.payload->data() == multiple.payload->data());
        CHECK(borrowed.unprotected.kid->data() == multiple.unprotected.kid->data());
        REQUIRE(borrowed.signatures.size() == 2);
        CHECK(borrowed.signatures[1].signature.data() == multiple.signatures[1].signature.data());

        const header_map text_label{.alg = std::nullopt, .kid = std::nullopt, .crit = {std::string{"application"}}};
        const auto       label_view = as_view(text_label);
        REQUIRE(label_view.crit.size() == 1);
        CHECK(std::get<std::string_view>(label_view.crit[0]).data() == std::get<std::string>(text_label.crit[0]).data());
    }

    TEST_CASE("sig structure views support four and five fields and replace stale state") {
        sig_structure      source{.context        = "Signature",
                                  .body_protected = {std::byte{1}},
                                  .sign_protected = byte_string{std::byte{2}},
                                  .external_aad   = {std::byte{3}},
                                  .payload        = {std::byte{4}}};
        sig_structure_view decoded = as_view(source);
        for (const bool with_signer : {true, false}) {
            if (!with_signer) {
                source.context = "Signature1";
                source.sign_protected.reset();
            }
            const auto bytes = encode_to_bytes(as_view(source));
            REQUIRE(bytes);
            REQUIRE(make_decoder<codec::cwt>(*bytes)(decoded));
            CHECK(decoded.context == source.context);
            CHECK(decoded.sign_protected.has_value() == with_signer);
            if (with_signer)
                CHECK(std::ranges::equal(*decoded.sign_protected, *source.sign_protected));
            CHECK(std::ranges::equal(decoded.body_protected, source.body_protected));
            CHECK(std::ranges::equal(decoded.external_aad, source.external_aad));
            CHECK(std::ranges::equal(decoded.payload, source.payload));
        }
    }

    TEST_CASE("cwt codec composes with nested optionals and pointer codecs") {
        const claims_set source{.issuer     = "nested",
                                .subject    = std::nullopt,
                                .audience   = std::nullopt,
                                .expiration = std::nullopt,
                                .not_before = std::nullopt,
                                .issued_at  = std::nullopt,
                                .cwt_id     = std::nullopt};
        const auto       projection = as_view(source);
        const auto       bytes      = encode_to_bytes(std::optional<claims_view>{projection});
        REQUIRE(bytes);
        std::optional<claims_view> decoded;
        REQUIRE(make_decoder<codec::cwt>(*bytes)(decoded));
        REQUIRE(decoded);
        CHECK(decoded->issuer == source.issuer);

        byte_string pointer_bytes;
        auto        pointer = std::make_unique<claims_view>(projection);
        REQUIRE((make_encoder<codec::cwt, codec::unique_ptr>(pointer_bytes)(pointer)));
        std::unique_ptr<claims_view> pointer_result;
        REQUIRE((make_decoder<codec::cwt, codec::unique_ptr>(pointer_bytes)(pointer_result)));
        REQUIRE(pointer_result);
        CHECK(pointer_result->issuer == source.issuer);
    }
}
