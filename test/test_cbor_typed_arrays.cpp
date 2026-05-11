#include "test_util.h"

#include <array>
#include <bit>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/rfc8746_typed_arrays.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

using namespace cbor::tags;
using namespace cbor::tags::ext::rfc8746;

namespace {

class toy_value {
  public:
    constexpr explicit toy_value(std::uint64_t value = 0) noexcept : value_(value) {}
    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return value_; }

    friend constexpr bool operator==(toy_value lhs, toy_value rhs) noexcept { return lhs.value_ == rhs.value_; }

  private:
    std::uint64_t value_{};
};

template <typename Self> struct toy_codec : cbor_codec_mixin_base<Self> {
    using cbor_codec_mixin_base<Self>::decode;
    using cbor_codec_mixin_base<Self>::encode;

    constexpr void encode(toy_value value) { static_cast<Self &>(*this).encode(value.value()); }

    constexpr status_code decode(toy_value &value, major_type major, std::byte additional_info) {
        std::uint64_t decoded{};
        auto          status = static_cast<Self &>(*this).decode(decoded, major, additional_info);
        if (status != status_code::success) {
            return status;
        }
        value = toy_value{decoded};
        return status_code::success;
    }
};

template <typename Enc, typename T>
concept CanEncode = requires(Enc &enc, const T &value) { enc.encode(value); };

template <typename T>
concept CanWrapAsTypedArray = requires(T &&values) { as_typed_array(std::forward<T>(values)); };

template <typename T>
concept HasTypedArrayPayloadBytes = requires(const T &view) { view.payload_bytes(); };

static_assert(cbor::tags::ext::rfc8746::detail::TypedArrayPayloadRange<std::span<const std::byte>>);
static_assert(!cbor::tags::ext::rfc8746::detail::TypedArrayPayloadRange<std::span<const std::uint16_t>>);

template <typename T> std::vector<std::byte> encode_normal(std::span<const T> values) {
    std::vector<std::byte> output;
    auto                   enc = make_encoder<typed_array_codec>(output);
    REQUIRE(enc(as_typed_array(values)));
    return output;
}

template <typename T> void check_values_equal(const std::vector<T> &observed, const std::vector<T> &expected) {
    if constexpr (std::same_as<T, float16_t>) {
        REQUIRE_EQ(observed.size(), expected.size());
        for (std::size_t i = 0; i < observed.size(); ++i) {
            CHECK_EQ(observed[i].value, expected[i].value);
        }
    } else {
        CHECK_EQ(observed, expected);
    }
}

template <typename T> void check_roundtrip(const std::vector<T> &values) {
    const auto encoded = encode_normal(std::span<const T>{values});

    {
        typed_array<T> decoded;
        auto           dec    = make_decoder<typed_array_codec>(encoded);
        const auto     result = dec(decoded);

        REQUIRE(result);
        check_values_equal(decoded.values(), values);
    }

    {
        typed_array_view<T> decoded;
        auto                dec    = make_decoder<typed_array_codec>(encoded);
        const auto          result = dec(decoded);

        REQUIRE(result);
        CHECK_EQ(decoded.payload_bytes().size(), values.size() * sizeof(T));
        CHECK_EQ(decoded.size(), values.size());
        check_values_equal(decoded.copy_values(), values);
    }
}

template <typename T> void check_decode_error(const char *hex, status_code expected) {
    const auto          bytes = to_bytes(hex);
    typed_array_view<T> decoded;
    auto                dec    = make_decoder<typed_array_codec>(bytes);
    const auto          result = dec(decoded);

    CAPTURE(hex);
    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), expected);
}

} // namespace

TEST_CASE("codec extensions append user mixins to default encoder and decoder") {
    using default_encoder   = decltype(make_encoder(std::declval<std::vector<std::byte> &>()));
    using extension_encoder = decltype(make_encoder<typed_array_codec>(std::declval<std::vector<std::byte> &>()));

    static_assert(!CanEncode<default_encoder, typed_array<std::int32_t>>);
    static_assert(CanEncode<extension_encoder, typed_array<std::int32_t>>);

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder<toy_codec>(buffer);
    REQUIRE(enc(toy_value{42}));

    toy_value decoded;
    auto      dec = make_decoder<toy_codec>(buffer);
    REQUIRE(dec(decoded));
    CHECK_EQ(decoded, toy_value{42});
}

TEST_CASE("rfc8746 typed arrays encode and decode supported element types through the opt-in codec") {
    check_roundtrip<std::int32_t>({-1, 0, 1, 123456});
    check_roundtrip<std::int64_t>({-1, 0, 1, 0x0102030405060708LL});
    check_roundtrip<float16_t>({float16_t{static_cast<std::uint16_t>(0x3C00)}, float16_t{static_cast<std::uint16_t>(0xC100)}});
    check_roundtrip<float>({-2.5F, 0.0F, 3.25F});
    check_roundtrip<double>({-2.5, 0.0, 3.25});
}

TEST_CASE("rfc8746 int32 typed array uses exact little-endian wire bytes") {
    const std::vector<std::int32_t> values{-1, -2, 0x01020304};

    const auto encoded = encode_normal(std::span<const std::int32_t>{values});

    CHECK_EQ(to_hex(encoded), "d84e4cfffffffffeffffff04030201");
}

TEST_CASE("rfc8746 float typed arrays use exact little-endian wire bytes") {
    {
        const std::vector<float16_t> values{float16_t{static_cast<std::uint16_t>(0x3C00)}, float16_t{static_cast<std::uint16_t>(0xC100)}};

        const auto encoded = encode_normal(std::span<const float16_t>{values});

        CHECK_EQ(to_hex(encoded), "d85444003c00c1");
    }

    {
        const std::vector<float> values{1.0F, -2.5F};

        const auto encoded = encode_normal(std::span<const float>{values});

        CHECK_EQ(to_hex(encoded), "d855480000803f000020c0");
    }

    {
        const std::vector<double> values{1.0, -2.5};

        const auto encoded = encode_normal(std::span<const double>{values});

        CHECK_EQ(to_hex(encoded), "d85650000000000000f03f00000000000004c0");
    }
}

TEST_CASE("rfc8746 int64 typed arrays use exact little-endian wire bytes") {
    const std::vector<std::int64_t> values{-1, 0x0102030405060708LL};

    const auto encoded = encode_normal(std::span<const std::int64_t>{values});

    CHECK_EQ(to_hex(encoded), "d84f50ffffffffffffffff0807060504030201");
}

TEST_CASE("rfc8746 typed array normal encoding matches flattened segments") {
    const std::vector<std::int32_t> values{-5, 42, 1000};
    const auto                      span   = std::span<const std::int32_t>{values};
    const auto                      normal = encode_normal(span);
    cbor_segments                   segments;

    if constexpr (std::endian::native == std::endian::little) {
        segments = encode_segments(as_typed_array(span));
    } else {
        segments = encode_typed_array_segments_copy(span);
    }

    CHECK_EQ(to_hex(normal), to_hex(flatten_segments(segments)));
}

TEST_CASE("rfc8746 typed array owned segment fallback matches normal encoding") {
    const std::vector<std::int64_t> values{-1, 0x0102030405060708LL};
    const auto                      span     = std::span<const std::int64_t>{values};
    const auto                      normal   = encode_normal(span);
    const auto                      segments = encode_typed_array_segments_copy(span);

    REQUIRE_EQ(segments.size(), 1U);
    CHECK(segments[0].is_owned());
    CHECK_EQ(to_hex(flatten_segments(segments)), to_hex(normal));
}

TEST_CASE("rfc8746 typed array segmented output borrows native little-endian payload") {
    if constexpr (std::endian::native == std::endian::little) {
        const std::vector<std::int32_t> values{1, -2, 3};
        const auto                      span           = std::span<const std::int32_t>{values};
        const auto                      original_bytes = std::as_bytes(span);

        const auto segments = encode_typed_array_segments(span);

        REQUIRE_EQ(segments.size(), 2U);
        CHECK(segments[0].is_owned());
        CHECK(segments[1].is_borrowed());
        CHECK_EQ(segments[1].data(), original_bytes.data());
        CHECK_EQ(segments[1].size(), original_bytes.size());
    }
}

TEST_CASE("rfc8746 typed array view exposes decoded values as a range") {
    const auto bytes = to_bytes("01000000feffffff03000000");
    const auto view  = typed_array_view<std::int32_t>{std::span<const std::byte>{bytes}};

    std::vector<std::int32_t> observed;
    for (auto value : view.values()) {
        observed.push_back(value);
    }

    CHECK_EQ(observed, std::vector<std::int32_t>{1, -2, 3});
    CHECK_EQ(view.copy_values(), observed);
}

TEST_CASE("rfc8746 typed array view reads unaligned payload bytes without a native span") {
    std::array<std::byte, sizeof(std::int32_t) + alignof(std::int32_t)> storage{};
    storage[0] = std::byte{0xCC};
    storage[1] = std::byte{0x01};
    storage[2] = std::byte{0x02};
    storage[3] = std::byte{0x03};
    storage[4] = std::byte{0x04};

    std::size_t offset = 1;
    if constexpr (alignof(std::int32_t) > 1U) {
        for (; offset < alignof(std::int32_t); ++offset) {
            const auto address = reinterpret_cast<std::uintptr_t>(storage.data() + offset);
            if ((address % alignof(std::int32_t)) != 0U) {
                break;
            }
        }
        REQUIRE(offset < alignof(std::int32_t));
    }

    storage[offset + 0U] = std::byte{0x01};
    storage[offset + 1U] = std::byte{0x02};
    storage[offset + 2U] = std::byte{0x03};
    storage[offset + 3U] = std::byte{0x04};

    const auto view = typed_array_view<std::int32_t>{std::span<const std::byte>{storage.data() + offset, sizeof(std::int32_t)}};

    CHECK_EQ(std::ranges::distance(view.values()), 1);
    CHECK_EQ(view.copy_values(), std::vector<std::int32_t>{0x04030201});
}

TEST_CASE("rfc8746 typed array view decodes non-contiguous definite byte strings without copying") {
    const auto encoded = encode_normal(std::span<const std::int32_t>{std::array{1, -2, 3}});
    const auto input   = std::deque<std::byte>{encoded.begin(), encoded.end()};

    {
        typed_array_view<std::int32_t> decoded;
        auto                           dec    = make_decoder<typed_array_codec>(input);
        const auto                     result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::contiguous_view_on_non_contiguous_data);
        CHECK_EQ(to_hex(std::ranges::subrange(dec.tell(), input.end())), "01000000feffffff03000000");
    }

    {
        auto dec                  = make_decoder<typed_array_codec>(input);
        using non_contiguous_view = typed_array_view_for<std::int32_t, decltype(dec)>;
        static_assert(std::same_as<typename non_contiguous_view::payload_range_type, typename decltype(dec)::bstr_view_t>);
        static_assert(!HasTypedArrayPayloadBytes<non_contiguous_view>);

        non_contiguous_view decoded;
        const auto          result = dec(decoded);

        REQUIRE(result);
        CHECK_EQ(decoded.size(), 3U);
        CHECK_EQ(decoded.copy_values(), std::vector<std::int32_t>{1, -2, 3});
        CHECK_EQ(to_hex(decoded.payload_range()), "01000000feffffff03000000");
    }

    {
        auto dec              = make_decoder<typed_array_codec>(encoded);
        using contiguous_view = typed_array_view_for<std::int32_t, decltype(dec)>;
        static_assert(std::same_as<typename contiguous_view::payload_range_type, std::span<const std::byte>>);
        static_assert(HasTypedArrayPayloadBytes<contiguous_view>);
    }

    {
        typed_array<std::int32_t> decoded;
        auto                      dec    = make_decoder<typed_array_codec>(input);
        const auto                result = dec(decoded);

        REQUIRE(result);
        CHECK_EQ(decoded.values(), std::vector<std::int32_t>{1, -2, 3});
    }
}

TEST_CASE("rfc8746 typed array borrowed encode wrapper rejects rvalue containers") {
    static_assert(CanWrapAsTypedArray<const std::vector<std::int32_t> &>);
    static_assert(!CanWrapAsTypedArray<std::vector<std::int32_t> &&>);
}

TEST_CASE("rfc8746 typed array decode rejects malformed inputs") {
    check_decode_error<std::int32_t>("d84f4400000000", status_code::no_match_for_tag);
    check_decode_error<std::int32_t>("d84e44010203", status_code::incomplete);
    check_decode_error<std::int32_t>("d84e43010203", status_code::unexpected_group_size);
}

TEST_CASE("rfc8746 typed array decode rejects invalid additional-info values") {
    for (const auto *hex : {"dc", "dd", "de", "df"}) {
        check_decode_error<std::int32_t>(hex, status_code::error);
    }

    for (const auto *hex : {"d84e5c", "d84e5d", "d84e5e"}) {
        check_decode_error<std::int32_t>(hex, status_code::error);
    }

    check_decode_error<std::int32_t>("d84e5f", status_code::no_match_for_bstr_on_buffer);
}

TEST_CASE("rfc8746 typed array decode rejects truncated extended headers and large claimed payloads") {
    for (const auto *hex : {"d8", "d94e", "da00004e", "db0000000000004e"}) {
        check_decode_error<std::int32_t>(hex, status_code::incomplete);
    }

    for (const auto *hex : {"d84e58", "d84e5900", "d84e5a000000", "d84e5b00000000000000"}) {
        check_decode_error<std::int32_t>(hex, status_code::incomplete);
    }

    check_decode_error<std::int32_t>("d84e5affffffff", status_code::incomplete);
    check_decode_error<std::int32_t>("d84e5b0000000100000000", status_code::incomplete);
}

TEST_CASE("rfc8746 typed array decode accepts non-minimal integer headers through the normal decoder") {
    const auto                     bytes = to_bytes("d9004e580401020304");
    typed_array_view<std::int32_t> decoded;
    auto                           dec    = make_decoder<typed_array_codec>(bytes);
    const auto                     result = dec(decoded);

    REQUIRE(result);
    CHECK_EQ(decoded.size(), 1U);
    CHECK_EQ(decoded.copy_values(), std::vector<std::int32_t>{0x04030201});
}
