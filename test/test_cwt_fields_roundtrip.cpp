#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/codec/cwt.h>
#include <doctest/doctest.h>

using namespace cbor::tags;

#include <list>

namespace custom_field_test {
struct text {
    std::string value;
};
template <typename Encoder> auto encode(Encoder &enc, const text &value) { return enc(value.value); }
template <typename Decoder> auto decode(Decoder &dec, text &&value) { return dec(value.value); }
struct number {
    int value{};
};

template <typename Self> struct codec : cbor::tags::codec::base<Self> {
    using cbor::tags::codec::base<Self>::encode;
    using cbor::tags::codec::base<Self>::decode;

    void        encode(const number &value) { static_cast<Self &>(*this).encode(value.value); }
    status_code decode(number &value) { return static_cast<Self &>(*this).decode(value.value); }
    status_code decode(number &value, major_type major, std::byte info) {
        return static_cast<Self &>(*this).decode(value.value, major, info);
    }
};
} // namespace custom_field_test

namespace {
template <typename T>
concept CanBindTemporary = requires { cwt::field<1>(T{}); };
static_assert(!CanBindTemporary<std::string>);
using One  = decltype(cwt::field<1>(std::declval<int &>()));
using Same = decltype(cwt::field<1U>(std::declval<int &>()));
static_assert(!cwt::detail::unique_field_labels<One, Same>::value);
} // namespace

TEST_SUITE("roundtrip/cwt") {
    TEST_CASE("custom fields retain the active codec pack") {
        custom_field_test::number source{12};
        auto                      source_fields = cwt::fields(cwt::field<1>(source));
        std::vector<std::byte>    buffer;
        REQUIRE(make_encoder<codec::cwt, custom_field_test::codec>(buffer)(source_fields));

        custom_field_test::number received;
        auto                      destination = cwt::fields(cwt::field<1>(received));
        REQUIRE(make_decoder<codec::cwt, custom_field_test::codec>(buffer)(destination));
        CHECK(received.value == source.value);
    }

    TEST_CASE("custom fields preserve ordinary ADL value customization") {
        custom_field_test::text source{"custom"};
        auto                    source_fields = cwt::fields(cwt::field<"custom">(source));
        std::vector<std::byte>  buffer;
        REQUIRE(make_encoder<codec::cwt>(buffer)(source_fields));

        custom_field_test::text received;
        using Decoder = decltype(make_decoder<codec::cwt>(buffer));
        static_assert(IsClassWithDecodingOverload<Decoder, custom_field_test::text>);
        auto destination = cwt::fields(cwt::field<"custom">(received));
        REQUIRE(make_decoder<codec::cwt>(buffer)(destination));
        CHECK(received.value == source.value);
    }

    TEST_CASE("custom fields select layouts and borrow text") {
        const std::string                issuer = "sender";
        const std::optional<std::string> role   = "reader";
        auto                             source = cwt::fields(cwt::field<1>(issuer), cwt::fields(cwt::field<"role">(role)));

        std::vector<std::byte> buffer;
        REQUIRE(make_encoder<codec::cwt>(buffer)(source));

        std::string_view   received;
        std::optional<int> absent      = 42;
        auto               destination = cwt::fields(cwt::field<1>(received), cwt::field<-1>(absent));
        REQUIRE(make_decoder<codec::cwt>(buffer)(destination));
        CHECK(received == issuer);
        CHECK_FALSE(absent);
        CHECK(received.data() >= reinterpret_cast<const char *>(buffer.data()));
        CHECK(received.data() + received.size() <= reinterpret_cast<const char *>(buffer.data() + buffer.size()));
    }

    TEST_CASE("custom fields decode owning values from unsized noncontiguous input") {
        std::string            source         = "payload";
        auto                   encoded_fields = cwt::fields(cwt::field<-2>(source));
        std::vector<std::byte> buffer;
        REQUIRE(make_encoder<codec::cwt>(buffer)(encoded_fields));

        std::list<std::byte> storage(buffer.begin(), buffer.end());
        auto                 input =
            std::ranges::subrange<std::list<std::byte>::iterator, std::list<std::byte>::iterator, std::ranges::subrange_kind::unsized>{
                storage.begin(), storage.end()};
        std::string received;
        auto        decoded_fields = cwt::fields(cwt::field<-2>(received));
        REQUIRE(make_decoder<codec::cwt>(input)(decoded_fields));
        CHECK(received == source);
    }

    TEST_CASE("custom optional fields decode the underlying type") {
        std::optional<int>     source         = 7;
        auto                   encoded_fields = cwt::fields(cwt::field<1>(source));
        std::vector<std::byte> buffer;
        REQUIRE(make_encoder<codec::cwt>(buffer)(encoded_fields));

        std::optional<int> received;
        auto               decoded_fields = cwt::fields(cwt::field<1>(received));
        REQUIRE(make_decoder<codec::cwt>(buffer)(decoded_fields));
        CHECK(received == source);
    }
}
