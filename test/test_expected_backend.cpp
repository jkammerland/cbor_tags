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
