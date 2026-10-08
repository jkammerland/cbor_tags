#include "cbor_tags/cwt/types.h"

#include <doctest/doctest.h>

namespace {

template <typename T>
concept HasAdlView = requires(T &&value) { as_view(std::forward<T>(value)); };

template <typename T> auto project(T &value) { return as_view(value); }

} // namespace

TEST_SUITE("roundtrip/cwt") {
    TEST_CASE("cwt projections are found through argument dependent lookup") {
        using namespace cbor::tags::cwt;

        static_assert(HasAdlView<claims_set &>);
        static_assert(HasAdlView<const claims_set &>);
        static_assert(!HasAdlView<claims_set>);
        static_assert(!HasAdlView<const claims_set>);

        claims_set claims{};
        claims.issuer   = "issuer";
        claims.cwt_id   = byte_string{std::byte{17}, std::byte{34}};
        const auto view = project(claims);
        static_assert(std::same_as<decltype(view), const claims_view>);
        REQUIRE(view.issuer);
        CHECK(*view.issuer == *claims.issuer);
        CHECK(view.issuer->data() == claims.issuer->data());
        REQUIRE(view.cwt_id);
        CHECK(view.cwt_id->data() == claims.cwt_id->data());
        CHECK(view.cwt_id->size() == claims.cwt_id->size());

        const header_map header{.alg = algorithm::es256, .kid = std::nullopt, .crit = {}};
        CHECK(project(header).alg == algorithm::es256);

        cose_sign message{};
        message.signatures.push_back(cose_signature{.protected_header = {}, .unprotected = {}, .signature = {std::byte{51}}});
        const auto envelope = project(message);
        REQUIRE(envelope.signatures.size() == 1);
        CHECK(envelope.signatures.front().signature.data() == message.signatures.front().signature.data());
        CHECK(project(message.signatures.front()).signature.data() == message.signatures.front().signature.data());

        cose_sign1 single{};
        single.signature = {std::byte{68}};
        CHECK(project(single).signature.data() == single.signature.data());

        const sig_structure structure{.context        = "Signature1",
                                      .body_protected = {},
                                      .sign_protected = std::nullopt,
                                      .external_aad   = {},
                                      .payload        = {}};
        CHECK(project(structure).context.data() == structure.context.data());
    }
}
