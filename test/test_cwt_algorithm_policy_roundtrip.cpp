#include "cwt_signing_test_support.h"

#include <cbor_tags/cwt/countersignatures.h>

using namespace cwt_test;

namespace algorithm_policy_test {

struct counting_backend {
    static constexpr algorithm algorithm_id = algorithm::es256;

    template <std::ranges::input_range Parts>
    static expected<void, status_code> verify(unsigned &calls, Parts &&, std::span<const std::byte>) {
        ++calls;
        return {};
    }

    template <std::ranges::input_range Parts>
    static expected<std::size_t, status_code> sign(unsigned &calls, Parts &&, std::span<std::byte>) {
        ++calls;
        return 0;
    }
};

struct extra {
    std::optional<unsigned> revision;
};

auto cwt_fields(extra &value) { return fields(field<"revision">(value.revision)); }
auto cwt_fields(const extra &value) { return fields(field<"revision">(value.revision)); }

using extended_header = basic_header_map<false, extra>;

template <class Message> void check_sign1_profiles(const Message &message, bool explicit_algorithm) {
    unsigned calls = 0;
    CHECK(bool(verify_sign1<counting_backend>(calls, message)) == explicit_algorithm);
    CHECK(calls == unsigned(explicit_algorithm));

    calls = 0;
    CHECK(bool(verify_sign1<counting_backend>(calls, message, implicit_algorithm_profile)) == !explicit_algorithm);
    CHECK(calls == unsigned(!explicit_algorithm));
}

template <class Message, class Signature>
void check_sign_profiles(const Message &message, const Signature &signature, bool explicit_algorithm) {
    unsigned calls = 0;
    CHECK(bool(verify_signature<counting_backend>(calls, message, signature)) == explicit_algorithm);
    CHECK(calls == unsigned(explicit_algorithm));

    calls = 0;
    CHECK(bool(verify_signature<counting_backend>(calls, message, signature, implicit_algorithm_profile)) == !explicit_algorithm);
    CHECK(calls == unsigned(!explicit_algorithm));

    calls = 0;
    CHECK(bool(verify_sign<counting_backend>(calls, message, std::size_t{0})) == explicit_algorithm);
    CHECK(calls == unsigned(explicit_algorithm));

    calls = 0;
    CHECK(bool(verify_sign<counting_backend>(calls, message, std::size_t{0}, implicit_algorithm_profile)) == !explicit_algorithm);
    CHECK(calls == unsigned(!explicit_algorithm));

    calls = 0;
    CHECK(bool(verify_sign<counting_backend>(calls, message)) == explicit_algorithm);
    CHECK(calls == unsigned(explicit_algorithm));

    calls = 0;
    CHECK(bool(verify_sign<counting_backend>(calls, message, implicit_algorithm_profile)) == !explicit_algorithm);
    CHECK(calls == unsigned(!explicit_algorithm));
}

} // namespace algorithm_policy_test

TEST_SUITE("roundtrip/cwt/algorithm_policy") {
    TEST_CASE("owning and borrowed Sign1 and Sign select explicit or implicit algorithm admission") {
        for (const bool explicit_algorithm : {false, true}) {
            auto header = encode_protected_header(header_map{.alg = explicit_algorithm ? std::optional{algorithm::es256} : std::nullopt});
            REQUIRE(header);

            cose_sign1 message{.protected_header = *header,
                               .unprotected      = {},
                               .payload          = std::vector<std::byte>{std::byte{1}},
                               .signature        = {std::byte{2}}};
            algorithm_policy_test::check_sign1_profiles(message, explicit_algorithm);

            auto encoded = encode_to_bytes(message);
            REQUIRE(encoded);
            cose_sign1_view view;
            REQUIRE(make_decoder<codec::cwt>(*encoded)(view));
            algorithm_policy_test::check_sign1_profiles(view, explicit_algorithm);

            cose_sign multi{.protected_header = {},
                            .unprotected      = {},
                            .payload          = message.payload,
                            .signatures       = {{.protected_header = *header, .unprotected = {}, .signature = {std::byte{2}}}}};
            algorithm_policy_test::check_sign_profiles(multi, multi.signatures.front(), explicit_algorithm);

            auto multi_encoded = encode_to_bytes(multi);
            REQUIRE(multi_encoded);
            cose_sign_view multi_view;
            REQUIRE(make_decoder<codec::cwt>(*multi_encoded)(multi_view));
            algorithm_policy_test::check_sign_profiles(multi_view, multi_view.signatures.front(), explicit_algorithm);
        }
    }

    TEST_CASE("mismatched algorithms and every missing signer are rejected before crypto") {
        auto mismatch = encode_protected_header(header_map{.alg = algorithm::es384});
        REQUIRE(mismatch);
        cose_sign1 message{.protected_header = *mismatch,
                           .unprotected      = {},
                           .payload          = std::vector<std::byte>{std::byte{1}},
                           .signature        = {}};
        unsigned   calls = 0;
        CHECK_FALSE(verify_sign1<algorithm_policy_test::counting_backend>(calls, message));
        CHECK_FALSE(verify_sign1<algorithm_policy_test::counting_backend>(calls, message, implicit_algorithm_profile));
        CHECK(calls == 0);

        auto matching = encode_protected_header(header_map{.alg = algorithm::es256});
        REQUIRE(matching);
        cose_sign multi{.protected_header = {},
                        .unprotected      = {},
                        .payload          = message.payload,
                        .signatures       = {{.protected_header = *matching, .unprotected = {}, .signature = {}}, {}}};
        CHECK_FALSE(verify_sign<algorithm_policy_test::counting_backend>(calls, multi, std::size_t{1}));
        CHECK(calls == 0);
        CHECK_FALSE(verify_sign<algorithm_policy_test::counting_backend>(calls, multi));
        CHECK(calls == 1);

        calls                                     = 0;
        multi.signatures.front().protected_header = *mismatch;
        CHECK_FALSE(verify_signature<algorithm_policy_test::counting_backend>(calls, multi, multi.signatures.front()));
        CHECK_FALSE(verify_sign<algorithm_policy_test::counting_backend>(calls, multi));
        CHECK(calls == 0);

        calls = 0;
        multi.signatures.front().protected_header.clear();
        REQUIRE(verify_sign<algorithm_policy_test::counting_backend>(calls, multi, implicit_algorithm_profile));
        CHECK(calls == 2);
    }

    TEST_CASE("typed Sign1 policy rejects before crypto critical processing and publication") {
        using namespace algorithm_policy_test;
        for (const std::optional<algorithm> alg :
             {std::optional<algorithm>{}, std::optional{algorithm::es384}, std::optional{algorithm::es256}}) {
            extended_header source{.alg = alg, .crit = {std::string{"revision"}}, .extra = {.revision = 42}};
            auto            header = encode_protected_header(source);
            REQUIRE(header);
            cose_sign1 message{.protected_header = *header,
                               .unprotected      = {},
                               .payload          = std::vector<std::byte>{std::byte{1}},
                               .signature        = {}};
            unsigned   calls     = 0;
            unsigned   processed = 0;
            auto       process   = [&](auto, auto, const auto &) -> expected<void, status_code> {
                ++processed;
                return {};
            };

            basic_header_map<true, extra> authenticated{.extra = {.revision = 9}};
            const bool                    explicit_valid = alg == algorithm::es256;
            CHECK(bool(verify_sign1<counting_backend>(calls, message, authenticated, process)) == explicit_valid);
            CHECK(calls == unsigned(explicit_valid));
            CHECK(processed == unsigned(explicit_valid));
            CHECK(authenticated.extra.revision == (explicit_valid ? 42U : 9U));

            calls = processed            = 0;
            authenticated.extra.revision = 9;
            CHECK(bool(verify_sign1<counting_backend>(calls, message, implicit_algorithm_profile, authenticated, process)) == !alg);
            CHECK(calls == unsigned(!alg));
            CHECK(processed == unsigned(!alg));
            CHECK(authenticated.extra.revision == (!alg ? 42U : 9U));
        }
    }

    TEST_CASE("typed signer verification applies policy before publishing body and signer headers") {
        using namespace algorithm_policy_test;
        for (const bool explicit_algorithm : {false, true}) {
            extended_header source{.alg   = explicit_algorithm ? std::optional{algorithm::es256} : std::nullopt,
                                   .crit  = {std::string{"revision"}},
                                   .extra = {.revision = 42}};
            auto            header = encode_protected_header(source);
            REQUIRE(header);
            cose_sign       message{.protected_header = {},
                                    .unprotected      = {},
                                    .payload          = std::vector<std::byte>{std::byte{1}},
                                    .signatures       = {{.protected_header = *header, .unprotected = {}, .signature = {}}}};
            header_map      body{.kid = std::vector<std::byte>{std::byte{9}}};
            extended_header signer{.extra = {.revision = 9}};
            unsigned        calls     = 0;
            unsigned        processed = 0;
            auto            process   = [&](auto, auto, const auto &) -> expected<void, status_code> {
                ++processed;
                return {};
            };

            CHECK(bool(verify_signature<counting_backend>(calls, message, message.signatures.front(), body, signer, process)) ==
                  explicit_algorithm);
            CHECK(calls == unsigned(explicit_algorithm));
            CHECK(processed == unsigned(explicit_algorithm));
            CHECK(signer.extra.revision == (explicit_algorithm ? 42U : 9U));
            CHECK(body.kid.has_value() == !explicit_algorithm);

            body.kid              = std::vector<std::byte>{std::byte{9}};
            signer.extra.revision = 9;
            calls = processed = 0;
            CHECK(bool(verify_sign<counting_backend>(calls, message, std::size_t{0}, body, signer, process)) == explicit_algorithm);
            CHECK(calls == unsigned(explicit_algorithm));
            CHECK(processed == unsigned(explicit_algorithm));
            CHECK(signer.extra.revision == (explicit_algorithm ? 42U : 9U));
            CHECK(body.kid.has_value() == !explicit_algorithm);

            body.kid              = std::vector<std::byte>{std::byte{9}};
            signer.extra.revision = 9;
            calls = processed = 0;
            CHECK(bool(verify_signature<counting_backend>(calls, message, message.signatures.front(), implicit_algorithm_profile, body,
                                                          signer, process)) == !explicit_algorithm);
            CHECK(calls == unsigned(!explicit_algorithm));
            CHECK(processed == unsigned(!explicit_algorithm));
            CHECK(signer.extra.revision == (!explicit_algorithm ? 42U : 9U));
            CHECK(body.kid.has_value() == explicit_algorithm);

            body.kid              = std::vector<std::byte>{std::byte{9}};
            signer.extra.revision = 9;
            calls = processed = 0;
            CHECK(bool(verify_sign<counting_backend>(calls, message, std::size_t{0}, implicit_algorithm_profile, body, signer, process)) ==
                  !explicit_algorithm);
            CHECK(calls == unsigned(!explicit_algorithm));
            CHECK(processed == unsigned(!explicit_algorithm));
            CHECK(signer.extra.revision == (!explicit_algorithm ? 42U : 9U));
            CHECK(body.kid.has_value() == explicit_algorithm);
        }
    }

    TEST_CASE("full and legacy countersignatures require the selected algorithm profile") {
        using namespace algorithm_policy_test;
        cose_sign1 target{.protected_header = {},
                          .unprotected      = {},
                          .payload          = std::vector<std::byte>{std::byte{1}},
                          .signature        = {std::byte{2}}};
        for (const bool explicit_algorithm : {false, true}) {
            auto header = encode_protected_header(header_map{.alg = explicit_algorithm ? std::optional{algorithm::es256} : std::nullopt});
            REQUIRE(header);
            cose_signature signature{.protected_header = *header, .unprotected = {}, .signature = {std::byte{3}}};
            unsigned       calls = 0;
            CHECK(bool(verify_countersignature<counting_backend>(calls, target, signature)) == explicit_algorithm);
            CHECK(calls == unsigned(explicit_algorithm));
            calls = 0;
            CHECK(bool(verify_legacy_countersignature<counting_backend>(calls, target, signature)) == explicit_algorithm);
            CHECK(calls == unsigned(explicit_algorithm));
            calls = 0;
            CHECK(bool(verify_countersignature<counting_backend>(calls, target, signature, implicit_algorithm_profile)) ==
                  !explicit_algorithm);
            CHECK(calls == unsigned(!explicit_algorithm));
            calls = 0;
            CHECK(bool(verify_legacy_countersignature<counting_backend>(calls, target, signature, implicit_algorithm_profile)) ==
                  !explicit_algorithm);
            CHECK(calls == unsigned(!explicit_algorithm));

            calls = 0;
            REQUIRE(verify_countersignature0<counting_backend>(calls, target, signature.signature));
            CHECK(calls == 1);
        }
    }

    TEST_CASE("typed full and legacy countersignatures admit policy before critical processing") {
        using namespace algorithm_policy_test;
        cose_signature target{.protected_header = {}, .unprotected = {}, .signature = {std::byte{1}}};
        for (const bool explicit_algorithm : {false, true}) {
            extended_header source{.alg   = explicit_algorithm ? std::optional{algorithm::es256} : std::nullopt,
                                   .crit  = {std::string{"revision"}},
                                   .extra = {.revision = 42}};
            auto            header = encode_protected_header(source);
            REQUIRE(header);
            cose_signature_view signature{.protected_header = *header, .unprotected = {}, .signature = {}};
            extended_header     authenticated{.extra = {.revision = 9}};
            unsigned            calls     = 0;
            unsigned            processed = 0;
            auto                process   = [&](auto, auto, const auto &) -> expected<void, status_code> {
                ++processed;
                return {};
            };

            CHECK(bool(verify_countersignature<counting_backend>(calls, target, signature, authenticated, process)) == explicit_algorithm);
            CHECK(calls == unsigned(explicit_algorithm));
            CHECK(processed == unsigned(explicit_algorithm));
            CHECK(authenticated.extra.revision == (explicit_algorithm ? 42U : 9U));

            calls = processed            = 0;
            authenticated.extra.revision = 9;
            CHECK(bool(verify_legacy_countersignature<counting_backend>(calls, target, signature, authenticated, process)) ==
                  explicit_algorithm);
            CHECK(calls == unsigned(explicit_algorithm));
            CHECK(processed == unsigned(explicit_algorithm));
            CHECK(authenticated.extra.revision == (explicit_algorithm ? 42U : 9U));

            calls = processed            = 0;
            authenticated.extra.revision = 9;
            CHECK(bool(verify_countersignature<counting_backend>(calls, target, signature, implicit_algorithm_profile, authenticated,
                                                                 process)) == !explicit_algorithm);
            CHECK(calls == unsigned(!explicit_algorithm));
            CHECK(processed == unsigned(!explicit_algorithm));
            CHECK(authenticated.extra.revision == (!explicit_algorithm ? 42U : 9U));

            calls = processed            = 0;
            authenticated.extra.revision = 9;
            CHECK(bool(verify_legacy_countersignature<counting_backend>(calls, target, signature, implicit_algorithm_profile, authenticated,
                                                                        process)) == !explicit_algorithm);
            CHECK(calls == unsigned(!explicit_algorithm));
            CHECK(processed == unsigned(!explicit_algorithm));
            CHECK(authenticated.extra.revision == (!explicit_algorithm ? 42U : 9U));
        }
    }

    TEST_CASE("caller storage full countersigning rejects absent algorithms before signing") {
        using namespace algorithm_policy_test;
        cose_signature            target{.protected_header = {}, .unprotected = {}, .signature = {std::byte{1}}};
        cose_signature            signature;
        unsigned                  calls = 0;
        std::array<std::byte, 64> output;
        CHECK_FALSE(countersign<counting_backend>(calls, target, signature, std::span<std::byte>{output}));
        CHECK(calls == 0);

        auto header = encode_protected_header(header_map{.alg = algorithm::es256});
        REQUIRE(header);
        signature.protected_header = *header;
        REQUIRE(countersign<counting_backend>(calls, target, signature, std::span<std::byte>{output}));
        CHECK(calls == 1);
    }
}
