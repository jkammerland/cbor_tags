#include "cwt_openssl_test_support.h"

#include <cbor_tags/cwt/countersignatures.h>

using namespace cwt_test;

namespace countersignature_application {
struct extra {
    std::optional<std::string> role;
};
auto cwt_fields(extra &value) { return fields(field<"role">(value.role)); }
auto cwt_fields(const extra &value) { return fields(field<"role">(value.role)); }
} // namespace countersignature_application

namespace countersignature_lifetimes {
struct backend {
    static constexpr algorithm algorithm_id = algorithm::esp256;
};
struct processor {
    template <typename Header> expected<void, status_code> operator()(header_scope, header_label_view, const Header &) const { return {}; }
};

template <typename Countersignature>
concept can_sign_with_parsed_headers =
    requires(cose_sign1 &target, Countersignature &&countersignature, header_map_view &parsed, std::span<std::byte> output) {
        countersign<backend>(nullptr, target, std::forward<Countersignature>(countersignature), parsed, output);
    };

template <typename Countersignature>
concept can_verify_with_parsed_headers = requires(cose_sign1 &target, Countersignature &&countersignature, header_map_view &parsed) {
    verify_countersignature<backend>(nullptr, target, std::forward<Countersignature>(countersignature), parsed, processor{});
};

template <typename Countersignature>
concept can_verify_legacy_with_parsed_headers = requires(cose_sign1 &target, Countersignature &&countersignature, header_map_view &parsed) {
    verify_legacy_countersignature<backend>(nullptr, target, std::forward<Countersignature>(countersignature), parsed, processor{});
};

template <typename Countersignature>
concept can_verify_implicit_with_parsed_headers =
    requires(cose_sign1 &target, Countersignature &&countersignature, header_map_view &parsed) {
        verify_countersignature<backend>(nullptr, target, std::forward<Countersignature>(countersignature), implicit_algorithm_profile,
                                         parsed, processor{});
    };

template <typename Countersignature>
concept can_verify_implicit_legacy_with_parsed_headers =
    requires(cose_sign1 &target, Countersignature &&countersignature, header_map_view &parsed) {
        verify_legacy_countersignature<backend>(nullptr, target, std::forward<Countersignature>(countersignature),
                                                implicit_algorithm_profile, parsed, processor{});
    };

static_assert(can_sign_with_parsed_headers<cose_signature &>);
static_assert(can_sign_with_parsed_headers<const cose_signature &>);
static_assert(!can_sign_with_parsed_headers<cose_signature>);
static_assert(!can_sign_with_parsed_headers<const cose_signature>);
static_assert(can_verify_with_parsed_headers<cose_signature &>);
static_assert(can_verify_with_parsed_headers<const cose_signature &>);
static_assert(!can_verify_with_parsed_headers<cose_signature>);
static_assert(!can_verify_with_parsed_headers<const cose_signature>);
static_assert(can_verify_legacy_with_parsed_headers<cose_signature &>);
static_assert(can_verify_legacy_with_parsed_headers<const cose_signature &>);
static_assert(!can_verify_legacy_with_parsed_headers<cose_signature>);
static_assert(!can_verify_legacy_with_parsed_headers<const cose_signature>);
static_assert(can_verify_implicit_with_parsed_headers<cose_signature &>);
static_assert(can_verify_implicit_with_parsed_headers<const cose_signature_view &>);
static_assert(!can_verify_implicit_with_parsed_headers<cose_signature>);
static_assert(!can_verify_implicit_with_parsed_headers<const cose_signature_view>);
static_assert(can_verify_implicit_legacy_with_parsed_headers<cose_signature &>);
static_assert(can_verify_implicit_legacy_with_parsed_headers<const cose_signature_view &>);
static_assert(!can_verify_implicit_legacy_with_parsed_headers<cose_signature>);
static_assert(!can_verify_implicit_legacy_with_parsed_headers<const cose_signature_view>);
} // namespace countersignature_lifetimes

TEST_SUITE("roundtrip/cwt/countersignatures") {
#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
    TEST_CASE("full countersignatures bind aad payload and the original signature independently") {
        auto                         key    = make_p256_key();
        auto                         notary = make_p256_key();
        const std::vector<std::byte> payload{std::byte{1}, std::byte{2}};
        const std::array             aad{std::byte{3}};
        auto                         target = sign1<openssl_esp256_backend>(key.get(), {}, {}, payload);
        REQUIRE(target);

        auto countersignature = countersign<openssl_esp256_backend>(notary.get(), *target, cose_signature{}, aad);
        REQUIRE(countersignature);
        REQUIRE(verify_countersignature<openssl_esp256_backend>(notary.get(), *target, *countersignature, aad));
        CHECK_FALSE(verify_countersignature<openssl_esp256_backend>(key.get(), *target, *countersignature, aad));
        CHECK_FALSE(verify_countersignature<openssl_esp256_backend>(notary.get(), *target, *countersignature));
        CHECK_FALSE(verify_legacy_countersignature<openssl_esp256_backend>(notary.get(), *target, *countersignature, aad));

        target->signature.front() ^= std::byte{1};
        CHECK_FALSE(verify_countersignature<openssl_esp256_backend>(notary.get(), *target, *countersignature, aad));
        target->signature.front() ^= std::byte{1};
        target->payload->front() ^= std::byte{1};
        CHECK_FALSE(verify_countersignature<openssl_esp256_backend>(notary.get(), *target, *countersignature, aad));
        target->payload->front() ^= std::byte{1};

        target->payload.reset();
        CHECK_FALSE(verify_countersignature<openssl_esp256_backend>(notary.get(), *target, *countersignature, aad));
        REQUIRE(verify_countersignature<openssl_esp256_backend>(notary.get(), *target, *countersignature, aad, payload));
    }

    TEST_CASE("countersignature chains and individual signer targets require separate original verification") {
        auto                         key    = make_p256_key();
        auto                         notary = make_p256_key();
        const std::vector<std::byte> payload{std::byte{1}};
        auto                         target = sign<openssl_esp256_backend>(key.get(), {}, {}, payload);
        REQUIRE(target);
        auto countersignature = countersign<openssl_esp256_backend>(notary.get(), target->signatures.front(), cose_signature{});
        REQUIRE(countersignature);
        auto chain = countersign<openssl_esp256_backend>(key.get(), *countersignature, cose_signature{});
        REQUIRE(chain);
        REQUIRE(verify_countersignature<openssl_esp256_backend>(key.get(), *countersignature, *chain));
        REQUIRE(verify_countersignature<openssl_esp256_backend>(notary.get(), target->signatures.front(), *countersignature));

        target->payload->front() ^= std::byte{1};
        CHECK_FALSE(verify_sign<openssl_esp256_backend>(key.get(), *target));
        CHECK(verify_countersignature<openssl_esp256_backend>(notary.get(), target->signatures.front(), *countersignature));
        countersignature->signature.front() ^= std::byte{1};
        CHECK_FALSE(verify_countersignature<openssl_esp256_backend>(key.get(), *countersignature, *chain));
    }

    TEST_CASE("abbreviated and full caller storage signatures stay distinct") {
        auto                         key = make_p256_key();
        const std::vector<std::byte> payload{std::byte{1}};
        auto                         target = sign1<openssl_esp256_backend>(key.get(), {}, {}, payload);
        REQUIRE(target);
        auto abbreviated = countersign0<openssl_esp256_backend>(key.get(), *target);
        REQUIRE(abbreviated);
        REQUIRE(verify_countersignature0<openssl_esp256_backend>(key.get(), *target, *abbreviated));

        auto header = encode_protected_header(header_map{.alg = algorithm::esp256});
        REQUIRE(header);
        std::array<std::byte, 64> storage;
        cose_signature_view       countersignature{.protected_header = *header, .unprotected = {}, .signature = storage};
        auto                      written = countersign<openssl_esp256_backend>(key.get(), *target, countersignature, storage);
        REQUIRE(written);
        CHECK(*written == storage.size());
        REQUIRE(verify_countersignature<openssl_esp256_backend>(key.get(), *target, countersignature));
        CHECK_FALSE(verify_countersignature0<openssl_esp256_backend>(key.get(), *target, storage));

        std::array<std::byte, 63> short_storage;
        CHECK_FALSE(countersign<openssl_esp256_backend>(key.get(), *target, countersignature, short_storage));
        CHECK_FALSE(countersign0<openssl_esp256_backend>(key.get(), *target, short_storage, {}));
        REQUIRE(countersign0<openssl_esp256_backend>(key.get(), *target, storage, {}));
        REQUIRE(verify_countersignature0<openssl_esp256_backend>(key.get(), *target, storage));
    }
    TEST_CASE("countersignature verification publishes custom critical headers only after authentication") {
        auto                         key = make_p256_key();
        const std::vector<std::byte> payload{std::byte{1}};
        auto                         target = sign1<openssl_esp256_backend>(key.get(), {}, {}, payload);
        REQUIRE(target);
        basic_header_map<false, countersignature_application::extra> header{.alg   = algorithm::esp256,
                                                                            .crit  = {std::string{"role"}},
                                                                            .extra = {.role = "notary"}};
        auto                                                         encoded = encode_protected_header(header);
        REQUIRE(encoded);

        std::array<std::byte, 64> storage;
        cose_signature_view       countersignature{.protected_header = *encoded, .unprotected = {}, .signature = storage};
        basic_header_map<false, countersignature_application::extra> parsed;
        REQUIRE(countersign<openssl_esp256_backend>(key.get(), *target, countersignature, parsed, storage));
        CHECK_FALSE(verify_countersignature<openssl_esp256_backend>(key.get(), *target, countersignature));

        bool called  = false;
        auto process = [&](header_scope scope, header_label_view label, const auto &value) -> expected<void, status_code> {
            called = true;
            CHECK(scope == header_scope::signature);
            CHECK(std::get<std::string_view>(label) == "role");
            return value.extra.role == "notary" ? expected<void, status_code>{}
                                                : expected<void, status_code>{cbor::tags::unexpected<status_code>{status_code::error}};
        };
        REQUIRE(verify_countersignature<openssl_esp256_backend>(key.get(), *target, countersignature, parsed, process));
        CHECK(called);
        CHECK(parsed.extra.role == "notary");

        called            = false;
        parsed.extra.role = "unchanged";
        storage.front() ^= std::byte{1};
        CHECK_FALSE(verify_countersignature<openssl_esp256_backend>(key.get(), *target, countersignature, parsed, process));
        CHECK_FALSE(called);
        CHECK(parsed.extra.role == "unchanged");
    }
#endif

    TEST_CASE("selected countersignature headers encode recursive chains and decode borrowed signature bytes") {
        using headers = countersignature_headers<>;
        headers                 source;
        headers::signature_type inner{.protected_header = {}, .unprotected = {}, .signature = {std::byte{1}}};
        headers::signature_type outer{.protected_header = {}, .unprotected = {}, .signature = {std::byte{2}}};
        outer.unprotected.extra.full.emplace().signatures.push_back(inner);
        source.full.emplace().signatures.push_back(outer);
        source.abbreviated = std::vector<std::byte>{std::byte{3}};

        basic_header_map<false, headers> header{.extra = source};
        auto                             bytes = encode_to_bytes(header);
        REQUIRE(bytes);
        basic_header_map<true, countersignature_headers_view> decoded;
        REQUIRE(make_decoder<codec::cwt>(*bytes)(decoded));
        REQUIRE(decoded.extra.full);
        REQUIRE(decoded.extra.full->signatures.size() == 1);
        const auto &countersignature = decoded.extra.full->signatures.front();
        CHECK(countersignature.signature.front() == std::byte{2});
        CHECK(countersignature.signature.data() >= bytes->data());
        CHECK(countersignature.signature.data() < bytes->data() + bytes->size());
        REQUIRE(countersignature.unprotected.extra.full);
        CHECK(countersignature.unprotected.extra.full->signatures.front().signature.front() == std::byte{1});
    }
}
