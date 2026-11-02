#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/codec/custom_1.h"
#include "cbor_tags/codec/typed_array.h"
#include "test_util.h"

#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <type_traits>
#include <utility>
#include <vector>

using namespace cbor::tags;

namespace customization_constraint_regressions {

struct member_value {
    std::uint32_t value{};

    template <typename Encoder> constexpr auto encode(Encoder &encoder) const {
        return encoder(static_tag<60001>{}, value);
    }

    template <typename Decoder> constexpr auto decode(Decoder &decoder) {
        return decoder(static_tag<60001>{}, value);
    }

    bool operator==(const member_value &) const = default;
};

struct adl_value {
    std::uint32_t value{};
    bool          operator==(const adl_value &) const = default;
};

template <typename Encoder> constexpr auto encode(Encoder &encoder, const adl_value &value) {
    return encoder(wrap_as_array{value.value});
}

template <typename Decoder> constexpr auto decode(Decoder &decoder, adl_value &&value) {
    return decoder(wrap_as_array{value.value});
}

struct transcode_value {
    std::int32_t value{};
    bool         operator==(const transcode_value &) const = default;
};

template <typename Transcoder> constexpr auto transcode(Transcoder &transcoder, const transcode_value &value) {
    return transcoder(static_tag<60003>{}, value.value);
}

template <typename Transcoder> constexpr auto transcode(Transcoder &transcoder, transcode_value &&value) {
    return transcoder(static_tag<60003>{}, value.value);
}

struct aggregate_value {
    std::uint8_t first{};
    std::uint8_t second{};
    bool         operator==(const aggregate_value &) const = default;
};

struct reversed_aggregate_value {
    std::uint8_t first{};
    std::uint8_t second{};

    template <typename Encoder> constexpr auto encode(Encoder &encoder) const {
        return encoder(wrap_as_array{second, first});
    }

    template <typename Decoder> constexpr auto decode(Decoder &decoder) {
        return decoder(wrap_as_array{second, first});
    }

    bool operator==(const reversed_aggregate_value &) const = default;
};

struct adl_tagged_value {
    std::uint8_t value{};
    bool         operator==(const adl_tagged_value &) const = default;
};

template <typename Encoder> constexpr auto encode(Encoder &encoder, const adl_tagged_value &value) {
    return encoder(static_tag<60005>{}, wrap_as_array{value.value});
}

template <typename Decoder> constexpr auto decode(Decoder &decoder, adl_tagged_value &&value) {
    return decoder(static_tag<60005>{}, wrap_as_array{value.value});
}

struct incompatible_value {
    template <typename Encoder> constexpr auto encode(Encoder &) const { return expected<void, int>{}; }
    template <typename Decoder> constexpr auto decode(Decoder &) { return expected<void, int>{}; }
};

using byte_buffer       = std::vector<std::byte>;
using no_extension_enc  = encoder<byte_buffer, default_options>;
using no_extension_dec  = decoder<byte_buffer, default_options>;
using one_extension_enc = encoder<byte_buffer, default_options, cbor_header_encoder>;
using one_extension_dec = decoder<byte_buffer, default_options, cbor_header_decoder>;
using default_shape_enc  = decltype(make_encoder(std::declval<byte_buffer &>()));
using default_shape_dec  = decltype(make_decoder(std::declval<byte_buffer &>()));
using typed_array_shape_enc      = decltype(make_encoder<codec::typed_array>(std::declval<byte_buffer &>()));
using typed_array_shape_dec      = decltype(make_decoder<codec::typed_array>(std::declval<byte_buffer &>()));
using two_extension_enc  = decltype(make_encoder<codec::typed_array, codec::custom_1>(std::declval<byte_buffer &>()));
using two_extension_dec  = decltype(make_decoder<codec::typed_array, codec::custom_1>(std::declval<byte_buffer &>()));

static_assert(std::is_aggregate_v<member_value>);
static_assert(IsAggregate<member_value>);
static_assert(detail::encoding_customization_traits<no_extension_enc, member_value>::has_overload ==
              IsClassWithEncodingOverload<no_extension_enc, member_value>);
static_assert(detail::decoding_customization_traits<no_extension_dec, member_value>::has_overload ==
              IsClassWithDecodingOverload<no_extension_dec, member_value>);
static_assert(detail::encoding_customization_traits<no_extension_enc, member_value>::compatible);
static_assert(detail::decoding_customization_traits<no_extension_dec, member_value>::compatible);
static_assert(!detail::encoding_customization_traits<no_extension_enc, member_value>::aggregate_fallback);
static_assert(!detail::decoding_customization_traits<no_extension_dec, member_value>::aggregate_fallback);

static_assert(IsAggregate<aggregate_value>);
static_assert(detail::encoding_customization_traits<typed_array_shape_enc, aggregate_value>::aggregate_fallback);
static_assert(detail::decoding_customization_traits<typed_array_shape_dec, aggregate_value>::aggregate_fallback);

static_assert(detail::encoding_customization_traits<typed_array_shape_enc, incompatible_value>::incompatible);
static_assert(detail::decoding_customization_traits<typed_array_shape_dec, incompatible_value>::incompatible);
static_assert(!detail::encoding_customization_traits<typed_array_shape_enc, incompatible_value>::compatible);
static_assert(!detail::decoding_customization_traits<typed_array_shape_dec, incompatible_value>::compatible);

static_assert(IsAggregate<reversed_aggregate_value>);
static_assert(detail::encoding_customization_traits<default_shape_enc, reversed_aggregate_value>::compatible);
static_assert(detail::decoding_customization_traits<default_shape_dec, reversed_aggregate_value>::compatible);
static_assert(detail::encoding_customization_traits<typed_array_shape_enc, reversed_aggregate_value>::compatible);
static_assert(detail::decoding_customization_traits<typed_array_shape_dec, reversed_aggregate_value>::compatible);
static_assert(detail::encoding_customization_traits<two_extension_enc, reversed_aggregate_value>::compatible);
static_assert(detail::decoding_customization_traits<two_extension_dec, reversed_aggregate_value>::compatible);
static_assert(!detail::encoding_customization_traits<default_shape_enc, reversed_aggregate_value>::aggregate_fallback);
static_assert(!detail::decoding_customization_traits<default_shape_dec, reversed_aggregate_value>::aggregate_fallback);
static_assert(!detail::encoding_customization_traits<typed_array_shape_enc, reversed_aggregate_value>::aggregate_fallback);
static_assert(!detail::decoding_customization_traits<typed_array_shape_dec, reversed_aggregate_value>::aggregate_fallback);
static_assert(!detail::encoding_customization_traits<two_extension_enc, reversed_aggregate_value>::aggregate_fallback);
static_assert(!detail::decoding_customization_traits<two_extension_dec, reversed_aggregate_value>::aggregate_fallback);

template <typename EncoderFactory, typename DecoderFactory>
void check_reversed_aggregate_factory(EncoderFactory &&encoder_factory, DecoderFactory &&decoder_factory) {
    byte_buffer               encoded;
    reversed_aggregate_value source{1, 2};
    auto                      encoder = encoder_factory(encoded);

    REQUIRE(encoder(source));
    CHECK_EQ(to_hex(encoded), "820201");

    const byte_buffer independent_input{std::byte{0x82}, std::byte{0x02}, std::byte{0x01}, std::byte{0x18}, std::byte{0x2a}};
    reversed_aggregate_value decoded{};
    std::uint8_t             trailing{};
    auto                     decoder = decoder_factory(independent_input);

    REQUIRE(decoder(decoded));
    CHECK_EQ(decoded, source);
    REQUIRE(decoder(trailing));
    CHECK_EQ(trailing, 42);
}

TEST_CASE("customization constraints preserve member dispatch over aggregate fallback with no extensions") {
    byte_buffer  encoded;
    member_value source{7};
    auto         encoder = no_extension_enc{encoded};

    REQUIRE(encoder(source));
    CHECK_EQ(to_hex(encoded), "d9ea6107");

    member_value decoded{};
    auto         decoder = no_extension_dec{encoded};
    REQUIRE(decoder(decoded));
    CHECK_EQ(decoded, source);
}

TEST_CASE("customization constraints preserve ADL dispatch with one extension") {
    byte_buffer encoded;
    adl_value   source{42};
    auto        encoder = one_extension_enc{encoded};

    REQUIRE(encoder(source));
    CHECK_EQ(to_hex(encoded), "81182a");

    adl_value decoded{};
    auto      decoder = one_extension_dec{encoded};
    REQUIRE(decoder(decoded));
    CHECK_EQ(decoded, source);
}

TEST_CASE("customization constraints preserve transcode dispatch with a typed-array extension pack") {
    byte_buffer     encoded;
    transcode_value source{-1};
    auto            encoder = make_encoder<codec::typed_array>(encoded);

    REQUIRE(encoder(source));
    CHECK_EQ(to_hex(encoded), "d9ea6320");

    transcode_value decoded{};
    auto            decoder = make_decoder<codec::typed_array>(encoded);
    REQUIRE(decoder(decoded));
    CHECK_EQ(decoded, source);
}

TEST_CASE("customization constraints preserve the default aggregate fallback") {
    byte_buffer     encoded;
    aggregate_value source{1, 2};
    auto            encoder = make_encoder(encoded);

    REQUIRE(encoder(source));
    CHECK_EQ(to_hex(encoded), "820102");

    aggregate_value decoded{};
    auto            decoder = make_decoder(encoded);
    REQUIRE(decoder(decoded));
    CHECK_EQ(decoded, source);
}

TEST_CASE("custom aggregate dispatch is stable across zero one and two extension factory packs") {
    SUBCASE("zero extension pack") {
        check_reversed_aggregate_factory([](auto &buffer) { return make_encoder(buffer); },
                                         [](const auto &buffer) { return make_decoder(buffer); });
    }
    SUBCASE("one extension pack") {
        check_reversed_aggregate_factory([](auto &buffer) { return make_encoder<codec::typed_array>(buffer); },
                                         [](const auto &buffer) { return make_decoder<codec::typed_array>(buffer); });
    }
    SUBCASE("two extension pack") {
        check_reversed_aggregate_factory([](auto &buffer) { return make_encoder<codec::typed_array, codec::custom_1>(buffer); },
                                         [](const auto &buffer) { return make_decoder<codec::typed_array, codec::custom_1>(buffer); });
    }
}

TEST_CASE("ADL tagged customization decodes independent bytes and leaves a trailing item") {
    byte_buffer       encoded;
    adl_tagged_value source{9};
    auto              encoder = make_encoder<codec::typed_array, codec::custom_1>(encoded);

    REQUIRE(encoder(source));
    CHECK_EQ(to_hex(encoded), "d9ea658109");

    const byte_buffer independent_input{std::byte{0xd9}, std::byte{0xea}, std::byte{0x65}, std::byte{0x81},
                                        std::byte{0x09}, std::byte{0x18}, std::byte{0x2a}};
    adl_tagged_value decoded{};
    std::uint8_t     trailing{};
    auto             decoder = make_decoder<codec::typed_array, codec::custom_1>(independent_input);

    REQUIRE(decoder(decoded));
    CHECK_EQ(decoded, source);
    REQUIRE(decoder(trailing));
    CHECK_EQ(trailing, 42);
}

} // namespace customization_constraint_regressions
