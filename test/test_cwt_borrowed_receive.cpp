#include "cwt_signing_test_support.h"

using namespace cwt_test;

namespace application {
struct identity {
    std::string_view user;
    std::int64_t     deadline;
};

// The decoder forwards its destination to the application's ADL customization.
template <typename Decoder> auto decode(Decoder &decoder, identity &&destination) -> typename Decoder::expected_type {
    claims_view fields;
    auto        result = decoder(fields);
    if (!result) {
        return result;
    }
    if (!fields.subject || !fields.expiration) {
        return unexpected<status_code>{status_code::error};
    }
    const auto *expiry = std::get_if<std::int64_t>(&*fields.expiration);
    if (!expiry) {
        return unexpected<status_code>{status_code::error};
    }
    destination = {.user = *fields.subject, .deadline = *expiry};
    return result;
}
} // namespace application

namespace {
void check_borrowed_from(std::span<const std::byte> field, const std::vector<std::byte> &buffer) {
    REQUIRE_FALSE(field.empty());
    const auto first = std::ranges::find_if(buffer, [&](const auto &byte) { return &byte == field.data(); });
    REQUIRE(first != buffer.end());
    CHECK(field.size() <= static_cast<std::size_t>(buffer.end() - first));
}
} // namespace

TEST_SUITE("roundtrip/cwt") {
    TEST_CASE("token output and receiver views retain original storage at each boundary") {
        const claims_set source{.subject = std::string(128, 's'), .expiration = std::int64_t{42}};
        auto             payload = encode_to_bytes(source);
        REQUIRE(payload);
        auto owned = sign1<signature_presence_es256_backend>(nullptr, {}, {}, *payload);
        REQUIRE(owned);
        const auto sent = as_view(*owned);

        byte_segments outgoing;
        REQUIRE(make_encoder<codec::cwt>(outgoing)(as_cwt(as_cose_sign1(sent))));
        for (const auto field : {sent.protected_header, *sent.payload, sent.signature}) {
            CHECK(std::ranges::any_of(outgoing, [&](const auto &segment) {
                return segment.is_borrowed() && segment.data() == field.data() && segment.size() == field.size();
            }));
        }

        const auto      bytes = outgoing.flatten();
        cose_sign1_view received;
        auto            cose    = make_tag_pair(cose_sign1_tag{}, received);
        auto            token   = make_tag_pair(cwt_tag{}, cose);
        auto            decoder = make_decoder<codec::cwt>(bytes);
        REQUIRE(decoder(token));
        REQUIRE(decoder.tell() == bytes.end());
        REQUIRE(received.payload);
        check_borrowed_from(received.protected_header, bytes);
        check_borrowed_from(*received.payload, bytes);
        check_borrowed_from(received.signature, bytes);
        REQUIRE(verify_sign1<signature_presence_es256_backend>(nullptr, received));

        application::identity identity{};
        auto                  claims_decoder = make_decoder<codec::cwt>(*received.payload);
        REQUIRE(claims_decoder(identity));
        CHECK(claims_decoder.tell() == received.payload->end());
        CHECK(identity.user == source.subject);
        CHECK(identity.deadline == 42);
        check_borrowed_from(std::as_bytes(std::span{identity.user.data(), identity.user.size()}), bytes);

        const std::string retained{identity.user};
        CHECK(retained == source.subject);
        CHECK(retained.data() != identity.user.data());
    }

    TEST_CASE("application claim requirements fail without overwriting its destination") {
        for (const auto &source : {claims_set{.expiration = std::int64_t{42}}, claims_set{.subject = "alice", .expiration = 42.5}}) {
            auto bytes = encode_to_bytes(source);
            REQUIRE(bytes);
            application::identity destination{.user = "previous", .deadline = 7};
            auto                  decoder = make_decoder<codec::cwt>(*bytes);

            CHECK_FALSE(decoder(destination));
            CHECK(destination.user == "previous");
            CHECK(destination.deadline == 7);
            CHECK(decoder.tell() == bytes->end());
        }
    }
}
