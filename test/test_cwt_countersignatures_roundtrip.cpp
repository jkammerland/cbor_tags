#include "cwt_openssl_test_support.h"

#include <cbor_tags/cwt/countersignatures.h>

using namespace cwt_test;

namespace counter_application {
struct extra {
    std::optional<std::string> role;
};
auto cwt_fields(extra &value) { return fields(field<"role">(value.role)); }
auto cwt_fields(const extra &value) { return fields(field<"role">(value.role)); }
} // namespace counter_application

namespace counter_lifetimes {
struct backend {
    static constexpr algorithm algorithm_id = algorithm::esp256;
};
struct processor {
    template <typename Header> expected<void, status_code> operator()(header_scope, header_label_view, const Header &) const { return {}; }
};

template <typename Counter>
concept can_sign_with_parsed_headers =
    requires(cose_sign1 &target, Counter &&counter, header_map_view &parsed, std::span<std::byte> output) {
        countersign<backend>(nullptr, target, std::forward<Counter>(counter), parsed, output);
    };

template <typename Counter>
concept can_verify_with_parsed_headers = requires(cose_sign1 &target, Counter &&counter, header_map_view &parsed) {
    verify_countersignature<backend>(nullptr, target, std::forward<Counter>(counter), parsed, processor{});
};

template <typename Counter>
concept can_verify_legacy_with_parsed_headers = requires(cose_sign1 &target, Counter &&counter, header_map_view &parsed) {
    verify_legacy_countersignature<backend>(nullptr, target, std::forward<Counter>(counter), parsed, processor{});
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
} // namespace counter_lifetimes

TEST_SUITE("roundtrip/cwt/countersignatures") {
#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
    TEST_CASE("full countersignatures bind aad payload and the original signature independently") {
        auto                         key    = make_p256_key();
        auto                         notary = make_p256_key();
        const std::vector<std::byte> payload{std::byte{1}, std::byte{2}};
        const std::array             aad{std::byte{3}};
        auto                         target = sign1<openssl_esp256_backend>(key.get(), {}, {}, payload);
        REQUIRE(target);

        auto counter = countersign<openssl_esp256_backend>(notary.get(), *target, cose_signature{}, aad);
        REQUIRE(counter);
        REQUIRE(verify_countersignature<openssl_esp256_backend>(notary.get(), *target, *counter, aad));
        CHECK_FALSE(verify_countersignature<openssl_esp256_backend>(key.get(), *target, *counter, aad));
        CHECK_FALSE(verify_countersignature<openssl_esp256_backend>(notary.get(), *target, *counter));
        CHECK_FALSE(verify_legacy_countersignature<openssl_esp256_backend>(notary.get(), *target, *counter, aad));

        target->signature.front() ^= std::byte{1};
        CHECK_FALSE(verify_countersignature<openssl_esp256_backend>(notary.get(), *target, *counter, aad));
        target->signature.front() ^= std::byte{1};
        target->payload->front() ^= std::byte{1};
        CHECK_FALSE(verify_countersignature<openssl_esp256_backend>(notary.get(), *target, *counter, aad));
        target->payload->front() ^= std::byte{1};

        target->payload.reset();
        CHECK_FALSE(verify_countersignature<openssl_esp256_backend>(notary.get(), *target, *counter, aad));
        REQUIRE(verify_countersignature<openssl_esp256_backend>(notary.get(), *target, *counter, aad, payload));
    }

    TEST_CASE("counter chains and individual signer targets require separate original verification") {
        auto                         key    = make_p256_key();
        auto                         notary = make_p256_key();
        const std::vector<std::byte> payload{std::byte{1}};
        auto                         target = sign<openssl_esp256_backend>(key.get(), {}, {}, payload);
        REQUIRE(target);
        auto counter = countersign<openssl_esp256_backend>(notary.get(), target->signatures.front(), cose_signature{});
        REQUIRE(counter);
        auto chain = countersign<openssl_esp256_backend>(key.get(), *counter, cose_signature{});
        REQUIRE(chain);
        REQUIRE(verify_countersignature<openssl_esp256_backend>(key.get(), *counter, *chain));
        REQUIRE(verify_countersignature<openssl_esp256_backend>(notary.get(), target->signatures.front(), *counter));

        target->payload->front() ^= std::byte{1};
        CHECK_FALSE(verify_sign<openssl_esp256_backend>(key.get(), *target));
        CHECK(verify_countersignature<openssl_esp256_backend>(notary.get(), target->signatures.front(), *counter));
        counter->signature.front() ^= std::byte{1};
        CHECK_FALSE(verify_countersignature<openssl_esp256_backend>(key.get(), *counter, *chain));
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
        cose_signature_view       counter{.protected_header = *header, .unprotected = {}, .signature = storage};
        auto                      written = countersign<openssl_esp256_backend>(key.get(), *target, counter, storage);
        REQUIRE(written);
        CHECK(*written == storage.size());
        REQUIRE(verify_countersignature<openssl_esp256_backend>(key.get(), *target, counter));
        CHECK_FALSE(verify_countersignature0<openssl_esp256_backend>(key.get(), *target, storage));

        std::array<std::byte, 63> short_storage;
        CHECK_FALSE(countersign<openssl_esp256_backend>(key.get(), *target, counter, short_storage));
        CHECK_FALSE(countersign0<openssl_esp256_backend>(key.get(), *target, short_storage, {}));
        REQUIRE(countersign0<openssl_esp256_backend>(key.get(), *target, storage, {}));
        REQUIRE(verify_countersignature0<openssl_esp256_backend>(key.get(), *target, storage));
    }
    TEST_CASE("counter verification publishes custom critical headers only after authentication") {
        auto                         key = make_p256_key();
        const std::vector<std::byte> payload{std::byte{1}};
        auto                         target = sign1<openssl_esp256_backend>(key.get(), {}, {}, payload);
        REQUIRE(target);
        basic_header_map<false, counter_application::extra> header{.alg   = algorithm::esp256,
                                                                   .crit  = {std::string{"role"}},
                                                                   .extra = {.role = "notary"}};
        auto                                                encoded = encode_protected_header(header);
        REQUIRE(encoded);

        std::array<std::byte, 64>                           storage;
        cose_signature_view                                 counter{.protected_header = *encoded, .unprotected = {}, .signature = storage};
        basic_header_map<false, counter_application::extra> parsed;
        REQUIRE(countersign<openssl_esp256_backend>(key.get(), *target, counter, parsed, storage));
        CHECK_FALSE(verify_countersignature<openssl_esp256_backend>(key.get(), *target, counter));

        bool called  = false;
        auto process = [&](header_scope scope, header_label_view label, const auto &value) -> expected<void, status_code> {
            called = true;
            CHECK(scope == header_scope::signature);
            CHECK(std::get<std::string_view>(label) == "role");
            return value.extra.role == "notary" ? expected<void, status_code>{}
                                                : expected<void, status_code>{cbor::tags::unexpected<status_code>{status_code::error}};
        };
        REQUIRE(verify_countersignature<openssl_esp256_backend>(key.get(), *target, counter, parsed, process));
        CHECK(called);
        CHECK(parsed.extra.role == "notary");

        called            = false;
        parsed.extra.role = "unchanged";
        storage.front() ^= std::byte{1};
        CHECK_FALSE(verify_countersignature<openssl_esp256_backend>(key.get(), *target, counter, parsed, process));
        CHECK_FALSE(called);
        CHECK(parsed.extra.role == "unchanged");
    }
#endif

    TEST_CASE("selected counter headers encode recursive chains and decode borrowed signature bytes") {
        using headers = counter_headers<>;
        headers                 source;
        headers::signature_type inner{.protected_header = {}, .unprotected = {}, .signature = {std::byte{1}}};
        headers::signature_type outer{.protected_header = {}, .unprotected = {}, .signature = {std::byte{2}}};
        outer.unprotected.extra.full.emplace().signatures.push_back(inner);
        source.full.emplace().signatures.push_back(outer);
        source.abbreviated = std::vector<std::byte>{std::byte{3}};

        basic_header_map<false, headers> header{.extra = source};
        auto                             bytes = encode_to_bytes(header);
        REQUIRE(bytes);
        basic_header_map<true, counter_headers_view> decoded;
        REQUIRE(make_decoder<codec::cwt>(*bytes)(decoded));
        REQUIRE(decoded.extra.full);
        REQUIRE(decoded.extra.full->signatures.size() == 1);
        const auto &counter = decoded.extra.full->signatures.front();
        CHECK(counter.signature.front() == std::byte{2});
        CHECK(counter.signature.data() >= bytes->data());
        CHECK(counter.signature.data() < bytes->data() + bytes->size());
        REQUIRE(counter.unprotected.extra.full);
        CHECK(counter.unprotected.extra.full->signatures.front().signature.front() == std::byte{1});
    }
}
