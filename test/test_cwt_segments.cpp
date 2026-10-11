#include "cwt_signing_test_support.h"

#include <array>
#include <utility>

using namespace cwt_test;

namespace {

template <typename Message>
concept CanBorrowSign1Input = requires(Message &&message) { make_sign1_tbs(std::forward<Message>(message)); };

template <typename Message, typename Signature>
concept CanBorrowSignInput = requires(Message &&message, Signature &&signature) {
    make_sign_tbs(std::forward<Message>(message), std::forward<Signature>(signature));
};

static_assert(CanBorrowSign1Input<cose_sign1 &> && CanBorrowSign1Input<const cose_sign1_view &>);
static_assert(!CanBorrowSign1Input<cose_sign1> && !CanBorrowSign1Input<const cose_sign1_view>);
static_assert(CanBorrowSignInput<const cose_sign &, const cose_signature_view &>);
static_assert(!CanBorrowSignInput<cose_sign, cose_signature &>);
static_assert(!CanBorrowSignInput<cose_sign &, cose_signature>);
static_assert(!CanBorrowSignInput<const cose_sign_view, const cose_signature_view>);

bool contains_borrowed(const byte_segments &segments, std::span<const std::byte> bytes) {
    return std::ranges::any_of(segments, [&](const auto &segment) {
        return segment.is_borrowed() && segment.data() == bytes.data() && segment.size() == bytes.size();
    });
}

struct observed_input {
    std::vector<std::span<const std::byte>> expected;
    std::span<const std::byte>              signature;
    std::size_t                             calls{};
    bool                                    fail{};
};

struct observing_backend {
    static constexpr algorithm algorithm_id = algorithm::es256;

    template <std::ranges::input_range Parts>
    static expected<void, status_code> verify(observed_input &observation, Parts &&parts, std::span<const std::byte> signature) {
        ++observation.calls;
        std::vector<std::span<const std::byte>> received;
        for (const auto bytes : parts) {
            received.push_back(bytes);
        }
        for (const auto wanted : observation.expected) {
            CHECK(
                std::ranges::any_of(received, [&](auto bytes) { return bytes.data() == wanted.data() && bytes.size() == wanted.size(); }));
        }
        CHECK(signature.data() == observation.signature.data());
        CHECK(signature.size() == observation.signature.size());
        if (observation.fail) {
            return cbor::tags::unexpected<status_code>{status_code::error};
        }
        return {};
    }
};

} // namespace

TEST_SUITE("roundtrip/cwt") {
    TEST_CASE("signature input borrows fields from owning and borrowed models") {
        const std::vector<std::byte> aad(31, std::byte{3});
        cose_sign1                   message{.protected_header = {std::byte{1}},
                                             .unprotected      = {},
                                             .payload          = std::vector<std::byte>(4096, std::byte{2}),
                                             .signature        = {}};

        const auto check = [&](const auto &model) {
            auto input = make_sign1_tbs(model, aad);
            REQUIRE(input);
            CHECK(contains_borrowed(*input, message.protected_header));
            CHECK(contains_borrowed(*input, *message.payload));
            CHECK(contains_borrowed(*input, aad));

            auto moved = std::move(*input);
            CHECK(contains_borrowed(moved, *message.payload));
            CHECK(moved.flatten().size() > message.payload->size() + aad.size());
        };
        check(message);
        const auto view = as_view(message);
        check(view);
    }

    TEST_CASE("multi signer input borrows body signer aad and detached payload") {
        const std::array aad{std::byte{3}};
        const std::array payload{std::byte{4}, std::byte{5}};
        cose_sign        message{.protected_header = {std::byte{1}}, .unprotected = {}, .payload = std::nullopt, .signatures = {}};
        cose_signature   signer{.protected_header = {std::byte{2}}, .unprotected = {}, .signature = {}};

        auto input = make_sign_tbs(message, signer, aad, std::span<const std::byte>{payload});
        REQUIRE(input);
        CHECK(contains_borrowed(*input, message.protected_header));
        CHECK(contains_borrowed(*input, signer.protected_header));
        CHECK(contains_borrowed(*input, aad));
        CHECK(contains_borrowed(*input, payload));
        CHECK_FALSE(make_sign_tbs(message, signer, aad));
    }

    TEST_CASE("verification passes received byte views directly to the backend") {
        const std::vector<std::byte> payload(4096, std::byte{7});
        const std::array             aad{std::byte{8}, std::byte{9}};
        auto                         source = sign1<signature_presence_es256_backend>(nullptr, {}, {}, payload);
        REQUIRE(source);
        auto bytes = encode_to_bytes(*source);
        REQUIRE(bytes);

        cose_sign1_view message;
        REQUIRE(make_decoder<codec::cwt>(*bytes)(message));
        REQUIRE(message.payload);
        observed_input observation{.expected = {message.protected_header, *message.payload, aad}, .signature = message.signature};

        REQUIRE(verify_sign1<observing_backend>(observation, message, aad));
        CHECK(observation.calls == 1);
        observation.fail = true;
        CHECK_FALSE(verify_sign1<observing_backend>(observation, message, aad));
        CHECK(observation.calls == 2);
    }
}
