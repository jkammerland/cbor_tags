#include "cwt_openssl_test_support.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/codec/cwt.h>
#include <doctest/doctest.h>

using namespace cbor::tags;

namespace custom_headers_test {
struct claims {
    std::string issuer;
};

template <typename Encoder> auto encode(Encoder &enc, const claims &value) {
    auto map = cwt::fields(cwt::field<1>(value.issuer));
    return enc(map);
}

template <typename Decoder> auto decode(Decoder &dec, claims &&value) {
    auto map = cwt::fields(cwt::field<1>(value.issuer));
    return dec(map);
}

struct extra {
    std::optional<std::uint64_t> revision;
    std::optional<claims>        application_claims;
};

auto cwt_fields(extra &value) { return cwt::fields(cwt::field<"revision">(value.revision), cwt::field<15>(value.application_claims)); }

auto cwt_fields(const extra &value) {
    return cwt::fields(cwt::field<"revision">(value.revision), cwt::field<15>(value.application_claims));
}
struct number {
    unsigned value{};
};
template <typename Self> struct number_codec : codec::base<Self> {
    using codec::base<Self>::encode;
    using codec::base<Self>::decode;
    void        encode(const number &value) { static_cast<Self &>(*this).encode(value.value); }
    status_code decode(number &value) { return static_cast<Self &>(*this).decode(value.value); }
    status_code decode(number &value, major_type major, std::byte info) {
        return static_cast<Self &>(*this).decode(value.value, major, info);
    }
};
struct custom_codec_headers {
    std::optional<number> revision;
};
auto cwt_fields(custom_codec_headers &value) { return cwt::fields(cwt::field<"revision">(value.revision)); }
auto cwt_fields(const custom_codec_headers &value) { return cwt::fields(cwt::field<"revision">(value.revision)); }

struct required_headers {
    unsigned revision{};
};
auto cwt_fields(required_headers &value) { return cwt::fields(cwt::field<"revision">(value.revision)); }
auto cwt_fields(const required_headers &value) { return cwt::fields(cwt::field<"revision">(value.revision)); }

} // namespace custom_headers_test

namespace lifetime_checks {
struct backend;
struct process {
    template <class Header> expected<void, status_code> operator()(cwt::header_scope, cwt::header_label_view, const Header &) const;
};
template <class Message>
concept TemporaryVerify = requires(cwt::header_map_view &parsed) { cwt::verify_sign1<backend>(0, Message{}, parsed, process{}); };
template <class Message>
concept TemporaryVerifySign = requires(cwt::header_map_view &body, cwt::header_map_view &signer) {
    cwt::verify_sign<backend>(0, Message{}, 0, body, signer, process{});
};
template <class Message>
concept TemporarySign =
    requires(cwt::cose_signature &signature, cwt::header_map_view &body, cwt::header_map_view &signer, std::span<std::byte> output) {
        cwt::sign_signature<backend>(0, Message{}, signature, body, signer, output);
    };
template <class Signature>
concept TemporarySigner =
    requires(cwt::cose_sign &message, cwt::header_map_view &body, cwt::header_map_view &signer, std::span<std::byte> output) {
        cwt::sign_signature<backend>(0, message, Signature{}, body, signer, output);
    };
static_assert(!TemporaryVerify<cwt::cose_sign1>);
static_assert(!TemporaryVerify<cwt::cose_sign1_view>);
static_assert(!TemporaryVerifySign<cwt::cose_sign>);
static_assert(!TemporarySign<cwt::cose_sign>);
static_assert(!TemporarySigner<cwt::cose_signature>);
} // namespace lifetime_checks

TEST_SUITE("roundtrip/cwt") {
    TEST_CASE("required extension fields reject absent empty protected header") {
        std::vector<std::byte> empty;
        std::vector<std::byte> empty_map;
        auto                   map = cwt::fields();
        REQUIRE(make_encoder<codec::cwt>(empty_map)(map));
        cwt::basic_header_map<false, custom_headers_test::required_headers> parsed;
        CHECK_FALSE(cwt::decode_protected_header(empty, parsed));
        CHECK_FALSE(cwt::decode_protected_header(empty_map, parsed));
    }

    TEST_CASE("custom headers roundtrip selected extension values and claims maps") {
        cwt::basic_header_map<false, custom_headers_test::extra> source;
        source.typ                      = std::string{"application/example+cwt"};
        source.extra.revision           = 3;
        source.extra.application_claims = custom_headers_test::claims{"issuer.example"};

        std::vector<std::byte> buffer;
        REQUIRE(make_encoder<codec::cwt>(buffer)(source));

        cwt::basic_header_map<false, custom_headers_test::extra> received;
        auto                                                     decode_result = make_decoder<codec::cwt>(buffer)(received);
        INFO(static_cast<int>(decode_result ? status_code::success : decode_result.error()));
        REQUIRE(decode_result);
        REQUIRE(received.extra.application_claims);
        CHECK(received.extra.application_claims->issuer == source.extra.application_claims->issuer);
        CHECK(received.extra.revision == source.extra.revision);
        CHECK(received.typ == source.typ);
    }

    TEST_CASE("receiver can discard values while observing their header labels") {
        cwt::basic_header_map<false, custom_headers_test::extra> source;
        source.extra.revision = 8;

        std::vector<std::byte> buffer;
        REQUIRE(make_encoder<codec::cwt>(buffer)(source));

        cwt::header_map received;
        REQUIRE(make_decoder<codec::cwt>(buffer)(received));
        REQUIRE(received.observed_labels.size() == 1);
        CHECK(std::get<std::string>(received.observed_labels.front()) == "revision");

        std::vector<std::byte> reencoded;
        REQUIRE(make_encoder<codec::cwt>(reencoded)(received));
        cwt::basic_header_map<false, custom_headers_test::extra> projected;
        REQUIRE(make_decoder<codec::cwt>(reencoded)(projected));
        CHECK_FALSE(projected.extra.revision);
    }
}

#if CBOR_TAGS_TEST_HAS_CWT_OPENSSL
TEST_SUITE("roundtrip/cwt") {
    TEST_CASE("authenticated custom headers preserve codecs and borrow protected bytes") {
        auto                                                                    key = cwt_test::make_p256_key();
        cwt::basic_header_map<false, custom_headers_test::custom_codec_headers> source;
        source.alg            = cwt::algorithm::esp256;
        source.typ            = std::string{"application/example+cwt"};
        source.extra.revision = custom_headers_test::number{4};
        auto protected_bytes  = cwt::encode_protected_header<custom_headers_test::number_codec>(source);
        REQUIRE(protected_bytes);

        cwt::cose_sign1 message;
        message.protected_header = *protected_bytes;
        message.payload          = std::vector<std::byte>{std::byte{1}};
        std::array<std::byte, 64>                                              signature;
        cwt::basic_header_map<true, custom_headers_test::custom_codec_headers> signing_headers;
        auto                                                                   written =
            cwt::sign1<cwt::openssl_esp256_backend, custom_headers_test::number_codec>(key.get(), message, signing_headers, signature);
        REQUIRE(written);
        message.signature.assign(signature.begin(), signature.begin() + *written);

        cwt::basic_header_map<true, custom_headers_test::custom_codec_headers> authenticated;
        auto process = [](auto, auto, const auto &) -> expected<void, status_code> {
            return cbor::tags::unexpected<status_code>{status_code::error};
        };
        REQUIRE(
            cwt::verify_sign1<cwt::openssl_esp256_backend, custom_headers_test::number_codec>(key.get(), message, authenticated, process));
        REQUIRE(authenticated.extra.revision);
        CHECK(authenticated.extra.revision->value == 4);
        REQUIRE(authenticated.typ);
        auto type = std::get<std::string_view>(*authenticated.typ);
        CHECK(type == "application/example+cwt");
        auto begin = reinterpret_cast<const char *>(message.protected_header.data());
        auto end   = begin + message.protected_header.size();
        CHECK(type.data() >= begin);
        CHECK(type.data() + type.size() <= end);
    }

    TEST_CASE("critical extension processing follows authentication and commits only on success") {
        auto                                                     key = cwt_test::make_p256_key();
        cwt::basic_header_map<false, custom_headers_test::extra> source;
        source.alg            = cwt::algorithm::esp256;
        source.crit           = {std::string{"revision"}};
        source.extra.revision = 3;
        auto protected_bytes  = cwt::encode_protected_header(source);
        REQUIRE(protected_bytes);

        cwt::cose_sign1 message;
        message.protected_header = *protected_bytes;
        message.payload          = std::vector<std::byte>{std::byte{1}};
        auto input               = cwt::make_sign1_tbs(message);
        REQUIRE(input);
        auto parts     = *input | std::views::transform([](const auto &segment) { return segment.bytes(); });
        auto signature = cwt::openssl_esp256_backend::sign(key.get(), parts);
        REQUIRE(signature);
        message.signature = *signature;

        cwt::basic_header_map<false, custom_headers_test::extra> authenticated;
        authenticated.extra.revision = 999;
        unsigned processed{};
        bool     accept{};
        auto     process = [&](auto scope, const auto &label, const auto &header) -> expected<void, status_code> {
            ++processed;
            CHECK(scope == cwt::header_scope::body);
            CHECK(std::get<std::string_view>(label) == "revision");
            CHECK(header.extra.revision == 3);
            return accept ? expected<void, status_code>{} : cbor::tags::unexpected<status_code>{status_code::error};
        };

        CHECK_FALSE(cwt::verify_sign1<cwt::openssl_esp256_backend>(key.get(), message, authenticated, process));
        CHECK(processed == 1);
        CHECK(authenticated.extra.revision == 999);

        accept = true;
        REQUIRE(cwt::verify_sign1<cwt::openssl_esp256_backend>(key.get(), message, authenticated, process));
        CHECK(processed == 2);
        CHECK(authenticated.extra.revision == 3);

        message.signature.front() ^= std::byte{1};
        CHECK_FALSE(cwt::verify_sign1<cwt::openssl_esp256_backend>(key.get(), message, authenticated, process));
        CHECK(processed == 2);
    }

    TEST_CASE("unprotected type and cross bucket extension duplicates fail verification") {
        auto            key = cwt_test::make_p256_key();
        cwt::header_map protected_header;
        protected_header.alg = cwt::algorithm::esp256;
        protected_header.typ = std::string{"application/example+cwt"};
        const std::vector<std::byte> payload{std::byte{0}};
        auto                         signed_message = cwt::sign1<cwt::openssl_esp256_backend>(key.get(), protected_header, {}, payload);
        REQUIRE(signed_message);

        cwt::header_map parsed;
        const auto      process = [](auto, auto, const auto &) -> expected<void, status_code> { return {}; };
        REQUIRE(cwt::verify_sign1<cwt::openssl_esp256_backend>(key.get(), *signed_message, parsed, process));
        signed_message->unprotected.typ = std::uint64_t{61};
        CHECK_FALSE(cwt::verify_sign1<cwt::openssl_esp256_backend>(key.get(), *signed_message, parsed, process));

        signed_message->unprotected.typ.reset();
        signed_message->unprotected.observed_labels = {integer{16}};
        CHECK_FALSE(cwt::verify_sign1<cwt::openssl_esp256_backend>(key.get(), *signed_message, parsed, process));
    }
}
#endif
