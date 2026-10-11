#include "cwt_openssl_test_support.h"

#include <array>

using namespace cwt_test;

namespace {

template <typename Bytes>
concept CanDecodeHeaderView =
    requires(Bytes &&bytes, header_map_view &header) { decode_protected_header(std::forward<Bytes>(bytes), header); };
static_assert(CanDecodeHeaderView<std::vector<std::byte> &>);
static_assert(CanDecodeHeaderView<const std::vector<std::byte> &>);
static_assert(!CanDecodeHeaderView<std::vector<std::byte>>);

struct buffer_key {
    std::vector<std::span<const std::byte>> expected;
    std::size_t                             calls{};
    bool                                    fail{};
};

// This backend observes storage and failures rather than implementing cryptography.
struct buffer_backend {
    static constexpr algorithm algorithm_id = algorithm::es256;

    template <std::ranges::input_range Parts>
    static expected<std::size_t, status_code> sign(buffer_key &key, Parts &&parts, std::span<std::byte> output) {
        ++key.calls;
        std::vector<std::span<const std::byte>> received;
        for (auto bytes : parts) {
            received.push_back(bytes);
        }
        for (auto wanted : key.expected) {
            CHECK(
                std::ranges::any_of(received, [&](auto bytes) { return bytes.data() == wanted.data() && bytes.size() == wanted.size(); }));
        }
        if (key.fail) {
            return cbor::tags::unexpected<status_code>{status_code::error};
        }
        if (output.empty()) {
            return cbor::tags::unexpected<status_code>{status_code::size_limit_exceeded};
        }
        output.front() = std::byte{17};
        return 1U;
    }
};

} // namespace

TEST_SUITE("roundtrip/cwt") {
    TEST_CASE("caller buffer signing borrows application fields and preserves the message") {
        struct application_token {
            std::vector<std::byte>      header;
            std::array<std::byte, 4096> body{};
            std::array<std::byte, 8>    signature{};
        };
        auto header = encode_protected_header(header_map{.alg = algorithm::es256});
        REQUIRE(header);
        application_token source{.header = std::move(*header)};
        source.signature.fill(std::byte{99});
        const std::array aad{std::byte{1}, std::byte{2}};
        cose_sign1_view  message{.protected_header = source.header, .unprotected = {}, .payload = source.body, .signature = {}};
        buffer_key       key{.expected = {source.header, source.body, aad}};

        auto result = sign1<buffer_backend>(key, message, source.signature, aad);
        REQUIRE(result);
        CHECK(*result == 1);
        CHECK(key.calls == 1);
        CHECK(source.signature.front() == std::byte{17});
        CHECK(std::ranges::all_of(std::span{source.signature}.subspan(1), [](auto byte) { return byte == std::byte{99}; }));
        CHECK(message.signature.empty());
        CHECK(message.payload->data() == source.body.data());
        CHECK(message.protected_header.data() == source.header.data());

        key.fail    = true;
        auto failed = sign1<buffer_backend>(key, message, source.signature, aad);
        REQUIRE_FALSE(failed);
        CHECK(failed.error() == status_code::error);
        CHECK(key.calls == 2);
    }

    TEST_CASE("caller buffer signing requires a protected algorithm before invoking the backend") {
        const std::array         payload{std::byte{1}};
        std::array<std::byte, 1> output{};
        cose_sign1_view          message{.protected_header = {}, .unprotected = {}, .payload = payload, .signature = {}};
        buffer_key               key{};

        CHECK_FALSE(sign1<buffer_backend>(key, message, output));
        auto wrong_header = encode_protected_header(header_map{.alg = algorithm::es384});
        REQUIRE(wrong_header);
        message.protected_header = *wrong_header;
        CHECK_FALSE(sign1<buffer_backend>(key, message, output));
        auto correct_header = encode_protected_header(header_map{.alg = algorithm::es256});
        REQUIRE(correct_header);
        message.protected_header = *correct_header;
        message.unprotected.alg  = algorithm::es256;
        CHECK_FALSE(sign1<buffer_backend>(key, message, output));
        CHECK(key.calls == 0);
        CHECK(output.front() == std::byte{});
    }

    TEST_CASE("caller buffer signing distinguishes absent and empty detached payloads") {
        auto header = encode_protected_header(header_map{.alg = algorithm::es256});
        REQUIRE(header);
        cose_sign1_view          message{.protected_header = *header, .unprotected = {}, .payload = std::nullopt, .signature = {}};
        std::array<std::byte, 1> output{};
        buffer_key               key{};

        CHECK_FALSE(sign1<buffer_backend>(key, message, output));
        CHECK(key.calls == 0);
        REQUIRE(sign1<buffer_backend>(key, message, output, {}, std::span<const std::byte>{}));
        CHECK(key.calls == 1);
    }

    TEST_CASE("protected header destinations choose ownership and clear on empty input") {
        const header_map source{.alg = algorithm::es256, .kid = std::vector<std::byte>(32, std::byte{7}), .crit = {integer{4}}};
        auto             bytes = encode_protected_header(source);
        REQUIRE(bytes);
        header_map_view borrowed;
        header_map      owned;

        REQUIRE(decode_protected_header(*bytes, borrowed));
        REQUIRE(decode_protected_header(*bytes, owned));
        REQUIRE(borrowed.kid);
        REQUIRE(owned.kid);
        CHECK(std::ranges::equal(*borrowed.kid, *source.kid));
        CHECK(*owned.kid == *source.kid);
        CHECK(std::ranges::any_of(*bytes, [&](const auto &byte) { return &byte == borrowed.kid->data(); }));
        CHECK(owned.kid->data() != borrowed.kid->data());

        const std::vector<std::byte> empty;
        REQUIRE(decode_protected_header(empty, borrowed));
        REQUIRE(decode_protected_header(empty, owned));
        CHECK(borrowed.empty());
        CHECK(owned.empty());
    }

#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
    TEST_CASE("openssl signs into exact and larger buffers without touching the suffix") {
        auto key    = make_p256_key();
        auto header = encode_protected_header(header_map{.alg = algorithm::es256});
        REQUIRE(header);
        const std::array payload{std::byte{7}};
        cose_sign1_view  message{.protected_header = *header, .unprotected = {}, .payload = payload, .signature = {}};

        for (const auto capacity : {64U, 80U}) {
            std::vector<std::byte> output(capacity, std::byte{99});
            auto                   result = sign1<crypto_es256_backend>(key.get(), message, output);
            REQUIRE(result);
            REQUIRE(*result == 64U);
            message.signature = std::span<const std::byte>{output}.first(*result);
            REQUIRE(verify_sign1<crypto_es256_backend>(key.get(), message));
            CHECK(std::ranges::all_of(std::span{output}.subspan(*result), [](auto byte) { return byte == std::byte{99}; }));
            message.signature = {};
        }

        std::array<std::byte, 63> short_output;
        short_output.fill(std::byte{99});
        auto result = sign1<crypto_es256_backend>(key.get(), message, short_output);
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::size_limit_exceeded);
        CHECK(std::ranges::all_of(short_output, [](auto byte) { return byte == std::byte{99}; }));
    }

    TEST_CASE("caller buffers hold two signatures over one borrowed payload") {
        auto first_key  = make_p256_key();
        auto second_key = make_p256_key();
        auto header     = encode_protected_header(header_map{.alg = algorithm::es256});
        REQUIRE(header);
        const std::array          payload{std::byte{7}, std::byte{9}};
        const std::array          aad{std::byte{5}};
        std::array<std::byte, 64> first_output{};
        std::array<std::byte, 64> second_output{};
        cose_sign_view            message{.protected_header = {}, .unprotected = {}, .payload = payload, .signatures = {}};
        cose_signature_view       first{.protected_header = *header, .unprotected = {}, .signature = {}};
        cose_signature_view       second = first;

        auto first_result  = sign_signature<crypto_es256_backend>(first_key.get(), message, first, first_output, aad);
        auto second_result = sign_signature<crypto_es256_backend>(second_key.get(), message, second, second_output, aad);
        REQUIRE(first_result);
        REQUIRE(second_result);
        first.signature    = std::span<const std::byte>{first_output}.first(*first_result);
        second.signature   = std::span<const std::byte>{second_output}.first(*second_result);
        message.signatures = {first, second};

        REQUIRE(verify_sign<crypto_es256_backend>(first_key.get(), message, std::size_t{0}, aad));
        REQUIRE(verify_sign<crypto_es256_backend>(second_key.get(), message, std::size_t{1}, aad));
        CHECK_FALSE(verify_sign<crypto_es256_backend>(first_key.get(), message, std::size_t{1}, aad));
        CHECK_FALSE(verify_sign<crypto_es256_backend>(first_key.get(), message, std::size_t{0}));
        CHECK(message.payload->data() == payload.data());
    }
#endif
}
