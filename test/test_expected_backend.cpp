#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_segments.h>
#include <cbor_tags/cbor_tags_config.h>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <type_traits>
#include <vector>

#if CBOR_TAGS_USE_STD_EXPECTED
#include <expected>
#else
#include <tl/expected.hpp>
#endif

using namespace cbor::tags;

namespace {

struct alternate_error {
    int value;
};

struct codec_error {
    status_code value;

    constexpr operator status_code() const noexcept { return value; }
};

struct status_result_without_bool {
    bool        success;
    status_code status;

    [[nodiscard]] constexpr bool        has_value() const noexcept { return success; }
    [[nodiscard]] constexpr status_code error() const noexcept { return status; }
};

struct failing_encoder_codec {
  private:
    friend cbor::tags::Access;

    template <typename Encoder> auto encode(Encoder &) const {
        return expected<void, codec_error>{cbor::tags::unexpected<codec_error>{codec_error{status_code::unexpected_group_size}}};
    }
};

struct failing_decoder_codec {
  private:
    friend cbor::tags::Access;

    template <typename Decoder> auto decode(Decoder &) {
        return expected<void, codec_error>{cbor::tags::unexpected<codec_error>{codec_error{status_code::invalid_utf8_sequence}}};
    }
};

struct status_only_encoder_codec {
  private:
    friend cbor::tags::Access;

    template <typename Encoder> auto encode(Encoder &) const { return status_result_without_bool{true, status_code::success}; }
};

struct status_only_member_transcode {
    template <typename Decoder> auto transcode(Decoder &) { return status_result_without_bool{false, status_code::unexpected_group_size}; }
};

struct status_only_member_decode {
    template <typename Decoder> auto decode(Decoder &) { return status_result_without_bool{false, status_code::invalid_utf8_sequence}; }
};

struct status_only_free_decode {};

template <typename Decoder> auto decode(Decoder &, status_only_free_decode &&) {
    return status_result_without_bool{false, status_code::no_match_for_tstr_on_buffer};
}

struct status_only_free_transcode {};

template <typename Decoder> auto transcode(Decoder &, status_only_free_transcode &&) {
    return status_result_without_bool{true, status_code::success};
}

#if CBOR_TAGS_USE_STD_EXPECTED
template <typename T> inline constexpr bool             is_configured_expected_v                       = false;
template <typename T> inline constexpr bool             is_configured_unexpected_v                     = false;
template <typename T, typename E> inline constexpr bool is_configured_expected_v<std::expected<T, E>>  = true;
template <typename E> inline constexpr bool             is_configured_unexpected_v<std::unexpected<E>> = true;
#else
template <typename T> inline constexpr bool             is_configured_expected_v                      = false;
template <typename T> inline constexpr bool             is_configured_unexpected_v                    = false;
template <typename T, typename E> inline constexpr bool is_configured_expected_v<tl::expected<T, E>>  = true;
template <typename E> inline constexpr bool             is_configured_unexpected_v<tl::unexpected<E>> = true;
#endif

} // namespace

TEST_CASE("configured expected backend preserves requested error type") {
    using result_type = expected<int, alternate_error>;

    static_assert(is_configured_expected_v<result_type>);
    static_assert(std::is_same_v<typename result_type::error_type, alternate_error>);

    result_type result = cbor::tags::unexpected<alternate_error>{alternate_error{7}};

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error().value, 7);
}

TEST_CASE("default return type uses configured expected backend") {
    static_assert(is_configured_expected_v<expected<void, status_code>>);
    static_assert(is_configured_expected_v<default_options::return_type>);
    static_assert(is_configured_unexpected_v<cbor::tags::unexpected<status_code>>);

    std::vector<std::byte> buffer;
    auto                   enc    = make_encoder(buffer);
    auto                   result = enc(std::uint64_t{1});

    static_assert(std::is_same_v<decltype(result), expected<void, status_code>>);
    static_assert(is_configured_expected_v<decltype(result)>);
    REQUIRE(result);
    CHECK_EQ(to_hex(buffer), "01");
}

TEST_CASE("configured expected backend carries decode errors") {
    const auto bytes = to_bytes("01");

    std::uint64_t value{};
    auto          dec    = make_decoder(bytes);
    auto          result = dec(value, std::uint64_t{2});

    static_assert(std::is_same_v<decltype(result), expected<void, status_code>>);
    static_assert(is_configured_expected_v<decltype(result)>);
    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::incomplete);
}

TEST_CASE("configured expected backend supports value-returning helpers") {
    auto encoded = encode_item_segments(std::uint64_t{42});

    static_assert(std::is_same_v<decltype(encoded), expected<encoded_item_segments, status_code>>);
    static_assert(is_configured_expected_v<decltype(encoded)>);
    REQUIRE(encoded);
    CHECK_EQ(to_hex(encoded->segments().front().bytes()), "182a");
}

TEST_CASE("custom codecs accept errors convertible to status code") {
    std::vector<std::byte> output;
    auto                   enc           = make_encoder(output);
    auto                   encode_result = enc(failing_encoder_codec{});

    static_assert(HasEncodeMethod<decltype(enc), failing_encoder_codec>);
    REQUIRE_FALSE(encode_result);
    CHECK_EQ(encode_result.error(), status_code::unexpected_group_size);
    CHECK(output.empty());

    const auto input = to_bytes("00");
    auto       dec   = make_decoder(input);
    auto       value = failing_decoder_codec{};

    static_assert(HasDecodeMethod<decltype(dec), failing_decoder_codec>);
    auto decode_result = dec(value);
    REQUIRE_FALSE(decode_result);
    CHECK_EQ(decode_result.error(), status_code::invalid_utf8_sequence);
}

TEST_CASE("custom codec results do not require contextual bool conversion") {
    static_assert(!std::convertible_to<status_result_without_bool, bool>);

    std::vector<std::byte> output;
    auto                   enc = make_encoder(output);
    REQUIRE(enc(status_only_encoder_codec{}));

    const auto input = to_bytes("00");

    status_only_member_transcode member_transcode;
    auto                         member_transcode_dec    = make_decoder(input);
    auto                         member_transcode_result = member_transcode_dec(member_transcode);
    REQUIRE_FALSE(member_transcode_result);
    CHECK_EQ(member_transcode_result.error(), status_code::unexpected_group_size);

    status_only_member_decode member_decode;
    auto                      member_decode_dec    = make_decoder(input);
    auto                      member_decode_result = member_decode_dec(member_decode);
    REQUIRE_FALSE(member_decode_result);
    CHECK_EQ(member_decode_result.error(), status_code::invalid_utf8_sequence);

    status_only_free_decode free_decode;
    auto                    free_decode_dec    = make_decoder(input);
    auto                    free_decode_result = free_decode_dec(free_decode);
    REQUIRE_FALSE(free_decode_result);
    CHECK_EQ(free_decode_result.error(), status_code::no_match_for_tstr_on_buffer);

    status_only_free_transcode free_transcode;
    auto                       free_transcode_dec = make_decoder(input);
    REQUIRE(free_transcode_dec(free_transcode));
}
