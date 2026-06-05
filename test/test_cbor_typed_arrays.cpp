#include "test_util.h"

#include <array>
#include <bit>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_lazy_tags.h>
#include <cbor_tags/extensions/rfc8746_typed_arrays.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using namespace cbor::tags;
using namespace cbor::tags::ext::rfc8746;
namespace rfc8746_detail = cbor::tags::ext::rfc8746::detail;
using cbor::tags::test::detail::allocation_failure_guard;

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

template <typename Dec, typename T>
concept CanDecode = requires(Dec &dec, T &value, major_type major, std::byte additional_info) {
    { dec.decode(value, major, additional_info) } -> std::same_as<status_code>;
};

template <typename T>
concept CanWrapAsTypedArray = requires(T &&values) { as_typed_array(std::forward<T>(values)); };

template <typename T>
concept CanEncodeTypedArrayBorrowedSegmentsBe = requires(T &&values) { encode_typed_array_borrowed_segments_be(std::forward<T>(values)); };

template <typename T>
concept CanEncodeTypedArraySegmentsBe = requires(T &&values) { encode_typed_array_segments_be(std::forward<T>(values)); };

template <typename T>
concept CanWrapAsHomogeneousArray = requires(T &&values) { as_homogeneous_array(std::forward<T>(values)); };

template <typename Dimensions, typename Array>
concept CanWrapAsMultiDimensionalArray = requires(Dimensions &&dimensions, Array &&values) {
    as_multi_dimensional_array(std::forward<Dimensions>(dimensions), std::forward<Array>(values));
};

template <typename Dimensions, typename Array>
concept CanWrapAsMultiDimensionalColumnMajorArray = requires(Dimensions &&dimensions, Array &&values) {
    as_multi_dimensional_column_major_array(std::forward<Dimensions>(dimensions), std::forward<Array>(values));
};

template <typename T>
concept HasTypedArrayPayloadBytes = requires(const T &view) { view.payload_bytes(); };

static_assert(rfc8746_detail::TypedArrayPayloadRange<std::span<const std::byte>>);
static_assert(!rfc8746_detail::TypedArrayPayloadRange<std::span<const std::uint16_t>>);
static_assert(IsTag<homogeneous_array<std::vector<int>>>);
static_assert(IsTag<multi_dimensional_array<std::vector<std::uint64_t>, typed_array<std::uint16_t>>>);
static_assert(IsTag<multi_dimensional_column_major_array<std::vector<std::uint64_t>, typed_array<std::uint16_t>>>);
static_assert(IsTag<typed_array<std::int32_t>>);
static_assert(IsTag<typed_array_view<std::int32_t>>);
static_assert(IsTag<typed_array_be<float>>);
static_assert(IsTag<typed_array_view_be<double>>);
static_assert(typed_array<std::uint8_t>::cbor_tag == 64U);
static_assert(typed_array_be<std::uint16_t>::cbor_tag == 65U);
static_assert(typed_array_be<std::uint32_t>::cbor_tag == 66U);
static_assert(typed_array_be<std::uint64_t>::cbor_tag == 67U);
static_assert(typed_array<uint8_clamped>::cbor_tag == 68U);
static_assert(typed_array<std::uint16_t>::cbor_tag == 69U);
static_assert(typed_array<std::uint32_t>::cbor_tag == 70U);
static_assert(typed_array<std::uint64_t>::cbor_tag == 71U);
static_assert(typed_array<std::int8_t>::cbor_tag == 72U);
static_assert(typed_array_be<std::int16_t>::cbor_tag == 73U);
static_assert(typed_array_be<std::int32_t>::cbor_tag == 74U);
static_assert(typed_array_be<std::int64_t>::cbor_tag == 75U);
static_assert(typed_array<std::int16_t>::cbor_tag == 77U);
static_assert(typed_array<std::int32_t>::cbor_tag == 78U);
static_assert(typed_array<std::int64_t>::cbor_tag == 79U);
static_assert(typed_array_be<float16_t>::cbor_tag == 80U);
static_assert(typed_array_be<float>::cbor_tag == 81U);
static_assert(typed_array_be<double>::cbor_tag == 82U);
static_assert(typed_array_be<float128_t>::cbor_tag == 83U);
static_assert(typed_array<float16_t>::cbor_tag == 84U);
static_assert(typed_array<float>::cbor_tag == 85U);
static_assert(typed_array<double>::cbor_tag == 86U);
static_assert(typed_array<float128_t>::cbor_tag == 87U);
static_assert(!IsTag<typed_array_ref<std::int32_t>>);
static_assert(!IsTag<homogeneous_array_ref<std::vector<int>>>);
static_assert(!IsTag<multi_dimensional_array_ref<std::vector<std::uint64_t>, typed_array<std::uint16_t>>>);

using extension_decoder = decltype(make_decoder<typed_array_codec>(std::declval<std::vector<std::byte> &>()));
using extension_encoder = decltype(make_encoder<typed_array_codec>(std::declval<std::vector<std::byte> &>()));
using bad_payload_range = std::ranges::iota_view<unsigned char, unsigned char>;
static_assert(rfc8746_detail::TypedArrayPayloadRange<bad_payload_range>);
static_assert(!CanDecode<extension_decoder, typed_array_view<std::int32_t, bad_payload_range>>);
static_assert(!CanEncode<extension_encoder, typed_array_view<std::int32_t>>);
static_assert(!CanEncode<extension_encoder, typed_array_view_be<double>>);
static_assert(IsCborMajor<bounded_size<typed_array<std::int32_t>, 1, 3>>);
static_assert(IsCborMajor<bounded_size<typed_array_ref<std::int32_t>, 1, 3>>);
static_assert(IsCborMajor<bounded_size<typed_array_view<std::int32_t>, 1, 3>>);
static_assert(CanEncode<extension_encoder, bounded_size<typed_array<std::int32_t>, 1, 3>>);
static_assert(CanEncode<extension_encoder, bounded_size<typed_array_ref<std::int32_t>, 1, 3>>);
static_assert(!CanEncode<extension_encoder, bounded_size<typed_array_view<std::int32_t>, 1, 3>>);
static_assert(CanDecode<extension_decoder, bounded_size<typed_array<std::int32_t>, 1, 3>>);
static_assert(!CanDecode<extension_decoder, bounded_size<typed_array_ref<std::int32_t>, 1, 3>>);
static_assert(CanDecode<extension_decoder, bounded_size<typed_array_view<std::int32_t>, 1, 3>>);
static_assert(!CanDecode<extension_decoder, bounded_size<typed_array_view<std::int32_t, bad_payload_range>, 1, 3>>);
static_assert(IsRFC8746ArrayPayload<typed_array_view<std::int32_t>>);
static_assert(!IsRFC8746EncodableArrayPayload<typed_array_view<std::int32_t>>);
static_assert(!CanWrapAsMultiDimensionalArray<std::vector<std::uint64_t> &, typed_array_view<std::int32_t> &>);
static_assert(!CanWrapAsMultiDimensionalColumnMajorArray<std::vector<std::uint64_t> &, typed_array_view<std::int32_t> &>);

using owning_payload_range = std::ranges::owning_view<std::vector<std::byte>>;
static_assert(!rfc8746_detail::TypedArrayPayloadRange<owning_payload_range>);

template <typename T> std::vector<std::byte> encode_normal(std::span<const T> values) {
    std::vector<std::byte> output;
    auto                   enc = make_encoder<typed_array_codec>(output);
    REQUIRE(enc(as_typed_array(values)));
    return output;
}

template <typename T> std::vector<std::byte> encode_big_endian(std::span<const T> values) {
    std::vector<std::byte> output;
    auto                   enc = make_encoder<typed_array_codec>(output);
    REQUIRE(enc(as_typed_array_be(values)));
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

template <typename T> void check_big_endian_roundtrip(const std::vector<T> &values) {
    const auto encoded = encode_big_endian(std::span<const T>{values});

    {
        typed_array_be<T> decoded;
        auto              dec    = make_decoder<typed_array_codec>(encoded);
        const auto        result = dec(decoded);

        REQUIRE(result);
        check_values_equal(decoded.values(), values);
    }

    {
        typed_array_view_be<T> decoded;
        auto                   dec    = make_decoder<typed_array_codec>(encoded);
        const auto             result = dec(decoded);

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

template <typename T> void check_big_endian_decode_error(const char *hex, status_code expected) {
    const auto             bytes = to_bytes(hex);
    typed_array_view_be<T> decoded;
    auto                   dec    = make_decoder<typed_array_codec>(bytes);
    const auto             result = dec(decoded);

    CAPTURE(hex);
    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), expected);
}

template <typename T> void check_big_endian_owned_decode_error(const char *hex, status_code expected) {
    const auto        bytes = to_bytes(hex);
    typed_array_be<T> decoded;
    auto              dec    = make_decoder<typed_array_codec>(bytes);
    const auto        result = dec(decoded);

    CAPTURE(hex);
    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), expected);
}

template <typename T> void check_big_endian_non_contiguous_decode_error(const char *hex, status_code expected) {
    const auto        bytes = to_bytes(hex);
    const auto        input = std::deque<std::byte>{bytes.begin(), bytes.end()};
    typed_array_be<T> decoded;
    auto              dec    = make_decoder<typed_array_codec>(input);
    const auto        result = dec(decoded);

    CAPTURE(hex);
    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), expected);
}

template <typename Segments> bool has_borrowed_segment(const Segments &segments, const std::byte *data, std::size_t size) {
    for (const auto &segment : segments) {
        if (segment.is_borrowed() && segment.data() == data && segment.size() == size) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("codec extensions append user mixins to default encoder and decoder") {
    using default_encoder   = decltype(make_encoder(std::declval<std::vector<std::byte> &>()));
    using default_decoder   = decltype(make_decoder(std::declval<std::vector<std::byte> &>()));
    using extension_encoder = decltype(make_encoder<typed_array_codec>(std::declval<std::vector<std::byte> &>()));

    static_assert(!CanEncode<default_encoder, typed_array<std::int32_t>>);
    static_assert(!CanDecode<default_decoder, typed_array<std::int32_t>>);
    static_assert(!CanDecode<default_decoder, typed_array_view<std::int32_t>>);
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
    const auto float128_value =
        float128_t{{std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}, std::byte{0x05}, std::byte{0x06},
                    std::byte{0x07}, std::byte{0x08}, std::byte{0x09}, std::byte{0x0A}, std::byte{0x0B}, std::byte{0x0C}, std::byte{0x0D},
                    std::byte{0x0E}, std::byte{0x0F}}};

    check_roundtrip<std::uint8_t>({0, 1, 255});
    check_roundtrip<std::uint16_t>({0, 1, 0x0102, 0xFFFE});
    check_roundtrip<std::uint32_t>({0, 1, 0x01020304U, 0xFFEEDDCCU});
    check_roundtrip<std::uint64_t>({0, 1, 0x0102030405060708ULL, 0xFFEEDDCCBBAA9988ULL});
    check_roundtrip<uint8_clamped>({uint8_clamped{static_cast<std::uint8_t>(0)}, uint8_clamped{static_cast<std::uint8_t>(128)},
                                    uint8_clamped{static_cast<std::uint8_t>(255)}});
    check_roundtrip<std::int8_t>({static_cast<std::int8_t>(-1), static_cast<std::int8_t>(0), static_cast<std::int8_t>(127)});
    check_roundtrip<std::int16_t>({-1, 0, 1, 0x0102});
    check_roundtrip<std::int32_t>({-1, 0, 1, 123456});
    check_roundtrip<std::int64_t>({-1, 0, 1, 0x0102030405060708LL});
    check_roundtrip<float16_t>({float16_t{static_cast<std::uint16_t>(0x3C00)}, float16_t{static_cast<std::uint16_t>(0xC100)}});
    check_roundtrip<float>({-2.5F, 0.0F, 3.25F});
    check_roundtrip<double>({-2.5, 0.0, 3.25});
    check_roundtrip<float128_t>({float128_value});
    check_big_endian_roundtrip<std::uint16_t>({0, 1, 0x0102, 0xFFFE});
    check_big_endian_roundtrip<std::uint32_t>({0, 1, 0x01020304U, 0xFFEEDDCCU});
    check_big_endian_roundtrip<std::uint64_t>({0, 1, 0x0102030405060708ULL, 0xFFEEDDCCBBAA9988ULL});
    check_big_endian_roundtrip<std::int16_t>({-1, 0, 1, 0x0102});
    check_big_endian_roundtrip<std::int32_t>({-1, 0, 1, 123456});
    check_big_endian_roundtrip<std::int64_t>({-1, 0, 1, 0x0102030405060708LL});
    check_big_endian_roundtrip<float16_t>({float16_t{static_cast<std::uint16_t>(0x3C00)}, float16_t{static_cast<std::uint16_t>(0xC100)}});
    check_big_endian_roundtrip<float>({-2.5F, 0.0F, 3.25F});
    check_big_endian_roundtrip<double>({-2.5, 0.0, 3.25});
    check_big_endian_roundtrip<float128_t>({float128_value});
}

TEST_CASE("rfc8746 typed arrays handle empty payloads across decode paths") {
    const std::vector<std::int32_t> values;
    const auto                      encoded = encode_normal(std::span<const std::int32_t>{values});
    CHECK_EQ(to_hex(encoded), "d84e40");

    {
        typed_array<std::int32_t> decoded;
        auto                      dec = make_decoder<typed_array_codec>(encoded);
        REQUIRE(dec(decoded));
        CHECK(decoded.values().empty());
    }

    {
        typed_array_view<std::int32_t> decoded;
        auto                           dec = make_decoder<typed_array_codec>(encoded);
        REQUIRE(dec(decoded));
        CHECK_EQ(decoded.size(), 0U);
        CHECK(decoded.copy_values().empty());
        CHECK(decoded.payload_bytes().empty());
    }

    {
        const auto segments = encode_typed_array_segments_copy(std::span<const std::int32_t>{values});
        CHECK_EQ(to_hex(flatten_segments(segments)), to_hex(encoded));
    }

    {
        auto tagged = std::vector<std::byte>{};
        auto enc    = make_encoder<typed_array_codec>(tagged);
        REQUIRE(enc(make_tag_pair(static_tag<100>{}, as_typed_array(values))));

        auto view = find_tags<100>(tagged);
        auto it   = view.begin();
        REQUIRE(it != view.end());

        typed_array<std::int32_t> decoded;
        REQUIRE(it->decode<typed_array_codec>(decoded));
        CHECK(decoded.values().empty());
    }
}

TEST_CASE("rfc8746 typed arrays use exact wire bytes for all integer tags") {
    const std::array<std::uint8_t, 3> u8_values{1, 2, 255};
    CHECK_EQ(to_hex(encode_normal(std::span<const std::uint8_t>{u8_values})), "d840430102ff");

    const std::array<uint8_clamped, 3> clamped_values{uint8_clamped{static_cast<std::uint8_t>(0)},
                                                      uint8_clamped{static_cast<std::uint8_t>(128)},
                                                      uint8_clamped{static_cast<std::uint8_t>(255)}};
    CHECK_EQ(to_hex(encode_normal(std::span<const uint8_clamped>{clamped_values})), "d844430080ff");

    const std::array<std::int8_t, 3> i8_values{static_cast<std::int8_t>(-1), static_cast<std::int8_t>(0), static_cast<std::int8_t>(127)};
    CHECK_EQ(to_hex(encode_normal(std::span<const std::int8_t>{i8_values})), "d84843ff007f");

    const std::array<std::uint16_t, 2> u16_values{0x0102, 0xFFFE};
    CHECK_EQ(to_hex(encode_big_endian(std::span<const std::uint16_t>{u16_values})), "d841440102fffe");
    CHECK_EQ(to_hex(encode_normal(std::span<const std::uint16_t>{u16_values})), "d845440201feff");

    const std::array<std::int16_t, 2> i16_values{-2, 0x0102};
    CHECK_EQ(to_hex(encode_big_endian(std::span<const std::int16_t>{i16_values})), "d84944fffe0102");
    CHECK_EQ(to_hex(encode_normal(std::span<const std::int16_t>{i16_values})), "d84d44feff0201");

    const std::array<std::uint32_t, 2> u32_values{0x01020304U, 0xFFEEDDCCU};
    CHECK_EQ(to_hex(encode_big_endian(std::span<const std::uint32_t>{u32_values})), "d8424801020304ffeeddcc");
    CHECK_EQ(to_hex(encode_normal(std::span<const std::uint32_t>{u32_values})), "d8464804030201ccddeeff");

    const std::array<std::int32_t, 2> i32_values{-1, 0x01020304};
    CHECK_EQ(to_hex(encode_big_endian(std::span<const std::int32_t>{i32_values})), "d84a48ffffffff01020304");

    const std::array<std::uint64_t, 2> u64_values{0x0102030405060708ULL, 0xFFEEDDCCBBAA9988ULL};
    CHECK_EQ(to_hex(encode_big_endian(std::span<const std::uint64_t>{u64_values})), "d843500102030405060708ffeeddccbbaa9988");
    CHECK_EQ(to_hex(encode_normal(std::span<const std::uint64_t>{u64_values})), "d8475008070605040302018899aabbccddeeff");

    const std::array<std::int64_t, 2> i64_values{-1, 0x0102030405060708LL};
    CHECK_EQ(to_hex(encode_big_endian(std::span<const std::int64_t>{i64_values})), "d84b50ffffffffffffffff0102030405060708");
}

TEST_CASE("rfc8746 typed arrays use exact wire bytes for all float tags") {
    const auto float128_value =
        float128_t{{std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}, std::byte{0x05}, std::byte{0x06},
                    std::byte{0x07}, std::byte{0x08}, std::byte{0x09}, std::byte{0x0A}, std::byte{0x0B}, std::byte{0x0C}, std::byte{0x0D},
                    std::byte{0x0E}, std::byte{0x0F}}};

    const std::array<float16_t, 2> f16_values{float16_t{static_cast<std::uint16_t>(0x3C00)}, float16_t{static_cast<std::uint16_t>(0xC100)}};
    CHECK_EQ(to_hex(encode_big_endian(std::span<const float16_t>{f16_values})), "d850443c00c100");

    const std::array<float, 2> f32_values{1.0F, -2.5F};
    CHECK_EQ(to_hex(encode_big_endian(std::span<const float>{f32_values})), "d851483f800000c0200000");

    const std::array<double, 2> f64_values{1.0, -2.5};
    CHECK_EQ(to_hex(encode_big_endian(std::span<const double>{f64_values})), "d852503ff0000000000000c004000000000000");

    const std::array<float128_t, 1> f128_values{float128_value};
    if constexpr (std::endian::native == std::endian::little) {
        CHECK_EQ(to_hex(encode_big_endian(std::span<const float128_t>{f128_values})), "d853500f0e0d0c0b0a09080706050403020100");
        CHECK_EQ(to_hex(encode_normal(std::span<const float128_t>{f128_values})), "d85750000102030405060708090a0b0c0d0e0f");
    } else {
        CHECK_EQ(to_hex(encode_big_endian(std::span<const float128_t>{f128_values})), "d85350000102030405060708090a0b0c0d0e0f");
        CHECK_EQ(to_hex(encode_normal(std::span<const float128_t>{f128_values})), "d857500f0e0d0c0b0a09080706050403020100");
    }
}

TEST_CASE("rfc8746 structural array tags encode and decode fixed-tag wrappers") {
    {
        const std::vector<int> values{1, 2, 3};
        std::vector<std::byte> bytes;
        auto                   enc = make_encoder<typed_array_codec>(bytes);

        REQUIRE(enc(as_homogeneous_array(values)));
        CHECK_EQ(to_hex(bytes), "d82983010203");

        homogeneous_array<std::vector<int>> decoded;
        auto                                dec = make_decoder<typed_array_codec>(bytes);
        REQUIRE(dec(decoded));
        CHECK_EQ(decoded.values(), values);
    }

    {
        const std::vector<std::uint64_t> dimensions{2, 2};
        const typed_array<std::uint16_t> values{{1, 2, 3, 4}};
        std::vector<std::byte>           bytes;
        auto                             enc = make_encoder<typed_array_codec>(bytes);

        REQUIRE(enc(as_multi_dimensional_array(dimensions, values)));
        CHECK_EQ(to_hex(bytes), "d82882820202d845480100020003000400");

        multi_dimensional_array<std::vector<std::uint64_t>, typed_array<std::uint16_t>> decoded;
        auto                                                                            dec = make_decoder<typed_array_codec>(bytes);
        REQUIRE(dec(decoded));
        CHECK_EQ(decoded.dimensions(), dimensions);
        CHECK_EQ(decoded.values().values(), values.values());
    }

    {
        const std::vector<std::uint64_t> dimensions{2, 2};
        const std::vector<std::uint16_t> values{1, 2, 3, 4};
        auto                             input = to_bytes("d82882820202d845480100020003000400");

        multi_dimensional_array<std::vector<std::uint64_t>, typed_array_view<std::uint16_t>> decoded;
        auto                                                                                 dec = make_decoder<typed_array_codec>(input);
        REQUIRE(dec(decoded));
        CHECK_EQ(decoded.dimensions(), dimensions);
        CHECK_EQ(decoded.values().copy_values(), values);
    }

    {
        const std::vector<std::uint64_t> dimensions{2, 2};
        const typed_array<std::uint16_t> values{{1, 2, 3, 4}};
        std::vector<std::byte>           bytes;
        auto                             enc = make_encoder<typed_array_codec>(bytes);

        REQUIRE(enc(as_multi_dimensional_column_major_array(dimensions, values)));
        CHECK_EQ(to_hex(bytes), "d9041082820202d845480100020003000400");

        using column_major_type = multi_dimensional_column_major_array<std::vector<std::uint64_t>, typed_array<std::uint16_t>>;
        std::variant<homogeneous_array<std::vector<int>>, column_major_type> decoded;
        auto                                                                 dec = make_decoder<typed_array_codec>(bytes);
        REQUIRE(dec(decoded));
        REQUIRE(std::holds_alternative<column_major_type>(decoded));
        CHECK_EQ(std::get<column_major_type>(decoded).dimensions(), dimensions);
        CHECK_EQ(std::get<column_major_type>(decoded).values().values(), values.values());
    }
}

TEST_CASE("rfc8746 structural array tags reject invalid multidimensional shapes") {
    {
        const std::vector<std::uint64_t> dimensions{};
        const typed_array<std::uint16_t> scalar{7};
        std::vector<std::byte>           bytes;
        auto                             enc = make_encoder<typed_array_codec>(bytes);

        REQUIRE(enc(as_multi_dimensional_array(dimensions, scalar)));
        CHECK_EQ(to_hex(bytes), "d8288280d845420700");

        multi_dimensional_array<std::vector<std::uint64_t>, typed_array<std::uint16_t>> decoded;
        auto                                                                            dec = make_decoder<typed_array_codec>(bytes);
        REQUIRE(dec(decoded));
        CHECK(decoded.dimensions().empty());
        CHECK_EQ(decoded.values().values(), scalar.values());
    }

    {
        const std::vector<std::uint64_t> dimensions{2, 0};
        const typed_array<std::uint16_t> values{{1, 2, 3, 4}};
        std::vector<std::byte>           bytes;
        auto                             enc = make_encoder<typed_array_codec>(bytes);

        const auto result = enc(as_multi_dimensional_array(dimensions, values));
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK(bytes.empty());
    }

    {
        const std::vector<std::uint64_t> dimensions{2, 3};
        const typed_array<std::uint16_t> values{{1, 2, 3, 4}};
        std::vector<std::byte>           bytes;
        auto                             enc = make_encoder<typed_array_codec>(bytes);

        const auto result = enc(as_multi_dimensional_array(dimensions, values));
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK(bytes.empty());
    }

    {
        const std::vector<std::uint64_t> dimensions{std::numeric_limits<std::uint64_t>::max(), 2};
        const typed_array<std::uint16_t> values{1};

        {
            std::vector<std::byte> bytes;
            auto                   enc    = make_encoder<typed_array_codec>(bytes);
            const auto             result = enc(as_multi_dimensional_array(dimensions, values));
            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), status_code::error);
            CHECK(bytes.empty());
        }

        {
            std::vector<std::byte> bytes;
            auto                   enc    = make_encoder<typed_array_codec>(bytes);
            const auto             result = enc(as_multi_dimensional_column_major_array(dimensions, values));
            REQUIRE_FALSE(result);
            CHECK_EQ(result.error(), status_code::error);
            CHECK(bytes.empty());
        }
    }

    {
        multi_dimensional_array<std::vector<std::uint64_t>, typed_array<std::uint16_t>> decoded;
        auto       input  = to_bytes("d82882820200d845480100020003000400");
        auto       dec    = make_decoder<typed_array_codec>(input);
        const auto result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::unexpected_group_size);
    }

    {
        multi_dimensional_array<std::vector<std::uint64_t>, typed_array<std::uint16_t>> decoded;
        auto       input  = to_bytes("d82882820203d845480100020003000400");
        auto       dec    = make_decoder<typed_array_codec>(input);
        const auto result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::unexpected_group_size);
    }

    {
        multi_dimensional_array<std::vector<std::uint64_t>, typed_array<std::uint16_t>> decoded;
        auto       input  = to_bytes("d82882821bffffffffffffffff02d845420100");
        auto       dec    = make_decoder<typed_array_codec>(input);
        const auto result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::unexpected_group_size);
    }

    {
        multi_dimensional_column_major_array<std::vector<std::uint64_t>, typed_array<std::uint16_t>> decoded;
        auto       input  = to_bytes("d9041082821bffffffffffffffff02d845420100");
        auto       dec    = make_decoder<typed_array_codec>(input);
        const auto result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::unexpected_group_size);
    }
}

TEST_CASE("rfc8746 structural array tags reject malformed payload wrappers") {
    {
        homogeneous_array<std::vector<int>> decoded;
        auto                                input  = to_bytes("d82901");
        auto                                dec    = make_decoder<typed_array_codec>(input);
        const auto                          result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_array_on_buffer);
    }

    {
        multi_dimensional_array<std::vector<std::uint64_t>, typed_array<std::uint16_t>> decoded;
        auto                                                                            input  = to_bytes("d82881820202");
        auto                                                                            dec    = make_decoder<typed_array_codec>(input);
        const auto                                                                      result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::unexpected_group_size);
    }

    {
        multi_dimensional_array<std::vector<std::uint64_t>, typed_array<std::uint16_t>> decoded;
        auto       input  = to_bytes("d82883820202d84548010002000300040080");
        auto       dec    = make_decoder<typed_array_codec>(input);
        const auto result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::unexpected_group_size);
    }

    {
        multi_dimensional_array<std::vector<std::uint64_t>, typed_array<std::uint16_t>> decoded;
        auto                                                                            input  = to_bytes("d8288201d845480100020003000400");
        auto                                                                            dec    = make_decoder<typed_array_codec>(input);
        const auto                                                                      result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_array_on_buffer);
    }

    {
        multi_dimensional_array<std::vector<std::uint64_t>, typed_array<std::uint16_t>> decoded;
        auto                                                                            input  = to_bytes("d8288282020201");
        auto                                                                            dec    = make_decoder<typed_array_codec>(input);
        const auto                                                                      result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_tag_on_buffer);
    }

    {
        multi_dimensional_array<std::vector<std::uint64_t>, typed_array<std::uint16_t>> decoded;
        auto                                                                            input  = to_bytes("d82882820202d84543010203");
        auto                                                                            dec    = make_decoder<typed_array_codec>(input);
        const auto                                                                      result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::unexpected_group_size);
    }

    {
        const std::vector<std::uint64_t> dimensions{2, 2};
        const typed_array<std::uint16_t> values{{1, 2, 3, 4}};
        std::vector<std::byte>           bytes;
        auto                             enc = make_encoder<typed_array_codec>(bytes);
        REQUIRE(enc(as_multi_dimensional_array(dimensions, values)));

        multi_dimensional_column_major_array<std::vector<std::uint64_t>, typed_array<std::uint16_t>> decoded;
        auto       dec    = make_decoder<typed_array_codec>(bytes);
        const auto result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_tag);
    }
}

TEST_CASE("rfc8746 typed arrays decode unambiguous variants by tag") {
    using value_type = std::variant<typed_array<std::int32_t>, typed_array<double>, static_tag<42>>;

    {
        value_type             encoded{typed_array<std::int32_t>{{1, -2, 3}}};
        std::vector<std::byte> bytes;
        auto                   enc = make_encoder<typed_array_codec>(bytes);

        REQUIRE(enc(encoded));
        CHECK_EQ(to_hex(bytes), "d84e4c01000000feffffff03000000");

        value_type decoded{static_tag<42>{}};
        auto       dec = make_decoder<typed_array_codec>(bytes);
        REQUIRE(dec(decoded));
        REQUIRE(std::holds_alternative<typed_array<std::int32_t>>(decoded));
        CHECK_EQ(std::get<typed_array<std::int32_t>>(decoded).values(), std::vector<std::int32_t>{1, -2, 3});
    }

    {
        const std::vector<double> values{1.0, -2.5};
        const auto                bytes = encode_normal(std::span<const double>{values});

        value_type decoded{static_tag<42>{}};
        auto       dec = make_decoder<typed_array_codec>(bytes);
        REQUIRE(dec(decoded));
        REQUIRE(std::holds_alternative<typed_array<double>>(decoded));
        CHECK_EQ(std::get<typed_array<double>>(decoded).values(), values);
    }
}

TEST_CASE("rfc8746 typed array views decode unambiguous variants by tag") {
    using value_type = std::variant<typed_array_view<std::int32_t>, static_tag<42>>;

    const std::vector<std::int32_t> values{1, -2, 3};
    const auto                      bytes = encode_normal(std::span<const std::int32_t>{values});

    value_type decoded{static_tag<42>{}};
    auto       dec = make_decoder<typed_array_codec>(bytes);
    REQUIRE(dec(decoded));
    REQUIRE(std::holds_alternative<typed_array_view<std::int32_t>>(decoded));
    CHECK_EQ(std::get<typed_array_view<std::int32_t>>(decoded).copy_values(), values);
}

TEST_CASE("rfc8746 big-endian typed arrays decode unambiguous variants by tag") {
    using value_type = std::variant<typed_array<float>, typed_array_be<float>, typed_array_view_be<double>>;

    {
        const std::vector<float> values{1.0F, -2.5F};
        const auto               bytes = encode_big_endian(std::span<const float>{values});

        value_type decoded{typed_array<float>{}};
        auto       dec = make_decoder<typed_array_codec>(bytes);
        REQUIRE(dec(decoded));
        REQUIRE(std::holds_alternative<typed_array_be<float>>(decoded));
        CHECK_EQ(std::get<typed_array_be<float>>(decoded).values(), values);
    }

    {
        const std::vector<double> values{1.0, -2.5};
        const auto                bytes = encode_big_endian(std::span<const double>{values});

        value_type decoded{typed_array<float>{}};
        auto       dec = make_decoder<typed_array_codec>(bytes);
        REQUIRE(dec(decoded));
        REQUIRE(std::holds_alternative<typed_array_view_be<double>>(decoded));
        CHECK_EQ(std::get<typed_array_view_be<double>>(decoded).copy_values(), values);

        const auto values_view = std::get<typed_array_view_be<double>>(decoded).values();
        CHECK(values_view);
        CHECK_FALSE(values_view.empty());
        CHECK_EQ(values_view.front(), values.front());
    }
}

TEST_CASE("rfc8746 typed arrays decode nested variants by tag") {
    using nested_type = std::variant<static_tag<42>, typed_array<double>>;
    using value_type  = std::variant<typed_array<std::int32_t>, nested_type>;

    const std::vector<double> values{1.0, -2.5};
    const auto                bytes = encode_normal(std::span<const double>{values});

    value_type decoded{typed_array<std::int32_t>{}};
    auto       dec = make_decoder<typed_array_codec>(bytes);
    REQUIRE(dec(decoded));
    REQUIRE(std::holds_alternative<nested_type>(decoded));
    const auto &nested = std::get<nested_type>(decoded);
    REQUIRE(std::holds_alternative<typed_array<double>>(nested));
    CHECK_EQ(std::get<typed_array<double>>(nested).values(), values);
}

TEST_CASE("rfc8746 typed array variant tag mismatches do not consume payload") {
    using value_type = std::variant<typed_array<std::int32_t>, static_tag<42>>;

    const auto bytes = to_bytes("d82a4401020304");
    auto       dec   = make_decoder<typed_array_codec>(bytes);

    value_type decoded{typed_array<std::int32_t>{}};
    REQUIRE(dec(decoded));
    CHECK(std::holds_alternative<static_tag<42>>(decoded));

    std::vector<std::byte> payload;
    REQUIRE(dec(payload));
    CHECK_EQ(to_hex(payload), "01020304");
}

TEST_CASE("rfc8746 typed array variants preserve malformed matching-tag errors") {
    using value_type = std::variant<typed_array<std::int32_t>, static_tag<42>>;

    const auto bytes = to_bytes("d84e43010203");
    auto       dec   = make_decoder<typed_array_codec>(bytes);

    value_type decoded{static_tag<42>{}};
    auto       result = dec(decoded);
    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::unexpected_group_size);
    CHECK(std::holds_alternative<static_tag<42>>(decoded));
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

TEST_CASE("rfc8746 float typed arrays use exact big-endian wire bytes") {
    {
        const std::vector<float> values{1.0F, -2.5F};

        const auto encoded = encode_big_endian(std::span<const float>{values});

        CHECK_EQ(to_hex(encoded), "d851483f800000c0200000");
    }

    {
        const std::vector<double> values{1.0, -2.5};

        const auto encoded = encode_big_endian(std::span<const double>{values});

        CHECK_EQ(to_hex(encoded), "d852503ff0000000000000c004000000000000");
    }
}

TEST_CASE("rfc8746 endian conversion helpers are reversible") {
    using rfc8746_detail::byteswap_bits;
    using rfc8746_detail::native_to_wire_bits;
    using rfc8746_detail::wire_to_native_bits;

    CHECK_EQ(byteswap_bits<std::uint16_t>(0x1234U), 0x3412U);
    CHECK_EQ(byteswap_bits<std::uint32_t>(0x12345678U), 0x78563412U);
    CHECK_EQ(byteswap_bits<std::uint64_t>(0x0102030405060708ULL), 0x0807060504030201ULL);

    const auto swapped_array =
        byteswap_bits(std::array{std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}, std::byte{0x05},
                                 std::byte{0x06}, std::byte{0x07}, std::byte{0x08}, std::byte{0x09}, std::byte{0x0A}, std::byte{0x0B},
                                 std::byte{0x0C}, std::byte{0x0D}, std::byte{0x0E}, std::byte{0x0F}});
    CHECK_EQ(swapped_array, std::array{std::byte{0x0F}, std::byte{0x0E}, std::byte{0x0D}, std::byte{0x0C}, std::byte{0x0B}, std::byte{0x0A},
                                       std::byte{0x09}, std::byte{0x08}, std::byte{0x07}, std::byte{0x06}, std::byte{0x05}, std::byte{0x04},
                                       std::byte{0x03}, std::byte{0x02}, std::byte{0x01}, std::byte{0x00}});

    constexpr auto value = std::uint64_t{0x0102030405060708ULL};
    CHECK_EQ(wire_to_native_bits<typed_array_byte_order::big>(native_to_wire_bits<typed_array_byte_order::big>(value)), value);
    CHECK_EQ(wire_to_native_bits<typed_array_byte_order::little>(native_to_wire_bits<typed_array_byte_order::little>(value)), value);
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

TEST_CASE("rfc8746 borrowed segmented output rejects converted byte order") {
    const std::vector<std::int32_t> values{1, -2, 3};
    const auto                      span = std::span<const std::int32_t>{values};
    cbor_segments                   segments;

    if constexpr (std::endian::native == std::endian::little) {
        CHECK_THROWS_AS((void)encode_typed_array_segments<typed_array_byte_order::big>(span), std::logic_error);
        CHECK_THROWS_AS(encode_typed_array_segments_into<typed_array_byte_order::big>(segments, span), std::logic_error);
    } else if constexpr (std::endian::native == std::endian::big) {
        CHECK_THROWS_AS((void)encode_typed_array_segments<typed_array_byte_order::little>(span), std::logic_error);
        CHECK_THROWS_AS(encode_typed_array_segments_into<typed_array_byte_order::little>(segments, span), std::logic_error);
    }
}

TEST_CASE("rfc8746 big-endian typed array owned segment fallback matches normal encoding") {
    const std::vector<double> values{1.0, -2.5};
    const auto                span     = std::span<const double>{values};
    const auto                normal   = encode_big_endian(span);
    const auto                segments = encode_typed_array_segments_copy_be(span);

    REQUIRE_EQ(segments.size(), 1U);
    CHECK(segments[0].is_owned());
    CHECK_EQ(to_hex(flatten_segments(segments)), to_hex(normal));
}

TEST_CASE("rfc8746 big-endian typed array segment helpers mirror explicit byte-order helpers") {
    const std::vector<double> values{1.0, -2.5};
    const auto                span          = std::span<const double>{values};
    const auto                explicit_copy = encode_typed_array_segments_copy<typed_array_byte_order::big>(span);
    const auto                named_copy    = encode_typed_array_segments_copy_be(span);

    CHECK_EQ(to_hex(flatten_segments(named_copy)), to_hex(flatten_segments(explicit_copy)));

    if constexpr (std::endian::native == std::endian::big) {
        const auto explicit_borrowed = encode_typed_array_borrowed_segments<typed_array_byte_order::big>(span);
        const auto named_borrowed    = encode_typed_array_borrowed_segments_be(span);
        const auto legacy_borrowed   = encode_typed_array_segments_be(span);

        CHECK_EQ(to_hex(flatten_segments(named_borrowed)), to_hex(flatten_segments(explicit_borrowed)));
        CHECK_EQ(to_hex(flatten_segments(legacy_borrowed)), to_hex(flatten_segments(explicit_borrowed)));
    } else {
        CHECK_THROWS_AS((void)encode_typed_array_borrowed_segments_be(span), std::logic_error);
        CHECK_THROWS_AS((void)encode_typed_array_segments_be(span), std::logic_error);
    }

    static_assert(CanEncodeTypedArrayBorrowedSegmentsBe<std::span<const double>>);
    static_assert(CanEncodeTypedArrayBorrowedSegmentsBe<std::span<double>>);
    static_assert(CanEncodeTypedArraySegmentsBe<std::span<const double>>);
    static_assert(CanEncodeTypedArraySegmentsBe<std::span<double>>);
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

TEST_CASE("rfc8746 typed array segmented into helper does not allocate after reserve") {
    if constexpr (std::endian::native == std::endian::little) {
        std::vector<std::int32_t> values{1, -2, 3};
        const auto                span           = std::span<std::int32_t>{values};
        const auto                original_bytes = std::as_bytes(span);

        cbor_segments segments;
        segments.reserve_segments(3);

        {
            allocation_failure_guard guard;
            encode_typed_array_segments_into(segments, span);
        }

        CHECK_EQ(to_hex(flatten_segments(segments)), to_hex(encode_normal(std::span<const std::int32_t>{values})));
        CHECK(has_borrowed_segment(segments, original_bytes.data(), original_bytes.size()));
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

TEST_CASE("rfc8746 typed array values view is safe when created from a temporary view") {
    const auto bytes  = to_bytes("01000000feffffff03000000");
    auto       values = typed_array_view<std::int32_t>{std::span<const std::byte>{bytes}}.values();

    static_assert(std::ranges::forward_range<decltype(values)>);
    CHECK_EQ(std::ranges::distance(values), 3);

    auto it = values.begin();
    REQUIRE(it != values.end());
    CHECK_EQ(*it, 1);
    ++it;
    REQUIRE(it != values.end());
    CHECK_EQ(*it, -2);
    ++it;
    REQUIRE(it != values.end());
    CHECK_EQ(*it, 3);
    ++it;
    CHECK(it == values.end());
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

    {
        const std::array<std::int32_t, 3> lazy_values{1, -2, 3};
        std::vector<std::byte>            tagged;
        auto                              enc = make_encoder<typed_array_codec>(tagged);
        REQUIRE(enc(make_tag_pair(static_tag<100>{}, as_typed_array(std::span<const std::int32_t>{lazy_values}))));

        std::deque<std::byte> tagged_input(tagged.begin(), tagged.end());
        auto                  tags = find_tags<100>(tagged_input);
        auto                  it   = tags.begin();
        REQUIRE(it != tags.end());

        auto payload_dec      = it->make_decoder<typed_array_codec>();
        using lazy_typed_view = typed_array_view_for<std::int32_t, decltype(payload_dec)>;
        static_assert(std::same_as<typename lazy_typed_view::payload_range_type, typename decltype(payload_dec)::bstr_view_t>);

        lazy_typed_view decoded;
        REQUIRE(payload_dec(decoded));
        CHECK_EQ(decoded.copy_values(), std::vector<std::int32_t>{1, -2, 3});
    }
}

TEST_CASE("rfc8746 big-endian typed array view decodes non-contiguous byte strings without copying") {
    const std::vector<double> values{1.0, -2.5};
    const auto                encoded = encode_big_endian(std::span<const double>{values});
    const auto                input   = std::deque<std::byte>{encoded.begin(), encoded.end()};

    {
        typed_array_view_be<double> decoded;
        auto                        dec    = make_decoder<typed_array_codec>(input);
        const auto                  result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::contiguous_view_on_non_contiguous_data);
        CHECK_EQ(to_hex(std::ranges::subrange(dec.tell(), input.end())), "3ff0000000000000c004000000000000");
    }

    auto dec                  = make_decoder<typed_array_codec>(input);
    using non_contiguous_view = typed_array_view_be_for<double, decltype(dec)>;
    static_assert(std::same_as<typename non_contiguous_view::payload_range_type, typename decltype(dec)::bstr_view_t>);
    static_assert(!HasTypedArrayPayloadBytes<non_contiguous_view>);

    non_contiguous_view decoded;
    const auto          result = dec(decoded);

    REQUIRE(result);
    CHECK_EQ(decoded.size(), values.size());
    CHECK_EQ(decoded.copy_values(), values);
    CHECK_EQ(to_hex(decoded.payload_range()), "3ff0000000000000c004000000000000");

    const auto values_view = decoded.values();
    CHECK(values_view);
    CHECK_FALSE(values_view.empty());
    CHECK_EQ(values_view.front(), values.front());
}

TEST_CASE("rfc8746 typed array borrowed encode wrapper rejects rvalue containers") {
    static_assert(CanWrapAsTypedArray<const std::vector<std::int32_t> &>);
    static_assert(!CanWrapAsTypedArray<std::vector<std::int32_t> &&>);

    using dimensions_type = std::vector<std::uint64_t>;
    using values_type     = std::vector<std::int32_t>;
    static_assert(CanWrapAsHomogeneousArray<const values_type &>);
    static_assert(!CanWrapAsHomogeneousArray<values_type &&>);
    static_assert(!CanWrapAsHomogeneousArray<int &>);
    static_assert(CanWrapAsMultiDimensionalArray<const dimensions_type &, const typed_array<std::int32_t> &>);
    static_assert(!CanWrapAsMultiDimensionalArray<const std::vector<int> &, const typed_array<std::int32_t> &>);
    static_assert(!CanWrapAsMultiDimensionalArray<const dimensions_type &, const int &>);
    static_assert(!CanWrapAsMultiDimensionalArray<dimensions_type &&, const typed_array<std::int32_t> &>);
    static_assert(!CanWrapAsMultiDimensionalArray<const dimensions_type &, typed_array<std::int32_t> &&>);
    static_assert(CanWrapAsMultiDimensionalColumnMajorArray<const dimensions_type &, const typed_array<std::int32_t> &>);
    static_assert(!CanWrapAsMultiDimensionalColumnMajorArray<dimensions_type &&, const typed_array<std::int32_t> &>);
    static_assert(!CanWrapAsMultiDimensionalColumnMajorArray<const dimensions_type &, typed_array<std::int32_t> &&>);
}

TEST_CASE("rfc8746 bounded typed arrays enforce element counts") {
    const auto encoded_three = to_bytes("d84e4c010000000200000003000000");

    {
        auto                   values = bounded_size<typed_array<std::int32_t>, 1, 3>{typed_array<std::int32_t>{{1, 2, 3}}};
        std::vector<std::byte> buffer;
        auto                   enc = make_encoder<typed_array_codec>(buffer);

        REQUIRE(enc(values));
        CHECK_EQ(to_hex(buffer), "d84e4c010000000200000003000000");
    }

    {
        auto                   values = bounded_size<typed_array<std::int32_t>, 1, 3>{typed_array<std::int32_t>{{1, 2, 3, 4}}};
        std::vector<std::byte> buffer;
        auto                   enc    = make_encoder<typed_array_codec>(buffer);
        const auto             result = enc(values);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK(buffer.empty());
    }

    {
        std::vector<std::int32_t> values{1, 2, 3};
        std::vector<std::byte>    buffer;
        auto                      enc = make_encoder<typed_array_codec>(buffer);

        REQUIRE(enc(as_bounded_size<1, 3>(as_typed_array(values))));
        CHECK_EQ(to_hex(buffer), "d84e4c010000000200000003000000");
    }

    {
        std::vector<std::int32_t> values{1, 2, 3, 4};
        std::vector<std::byte>    buffer;
        auto                      enc    = make_encoder<typed_array_codec>(buffer);
        const auto                result = enc(as_bounded_size<1, 3>(as_typed_array(values)));

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK(buffer.empty());
    }

    {
        bounded_size<typed_array<std::int32_t>, 1, 3> decoded;
        auto                                          dec = make_decoder<typed_array_codec>(encoded_three);

        REQUIRE(dec(decoded));
        CHECK_EQ(decoded.value().values(), (std::vector<std::int32_t>{1, 2, 3}));
    }

    {
        bounded_size<typed_array<std::int32_t>, 1, 3> decoded;
        auto                                          input  = to_bytes("d84e5001000000020000000300000004000000");
        auto                                          dec    = make_decoder<typed_array_codec>(input);
        const auto                                    result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK(decoded.value().values().empty());
    }

    {
        bounded_size<typed_array<std::int32_t>, 2, 3> decoded;
        auto                                          input  = to_bytes("d84e4401000000");
        auto                                          dec    = make_decoder<typed_array_codec>(input);
        const auto                                    result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::size_limit_exceeded);
        CHECK(decoded.value().values().empty());
    }

    {
        bounded_size<typed_array<std::int32_t>, 1, 3> decoded;
        auto                                          input  = to_bytes("d84e450100000000");
        auto                                          dec    = make_decoder<typed_array_codec>(input);
        const auto                                    result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::unexpected_group_size);
        CHECK(decoded.value().values().empty());
    }

    {
        bounded_size<typed_array_view<std::int32_t>, 1, 3> decoded;
        auto                                               dec = make_decoder<typed_array_codec>(encoded_three);

        REQUIRE(dec(decoded));
        CHECK_EQ(decoded.value().size(), 3U);
        CHECK_EQ(decoded.value().copy_values(), (std::vector<std::int32_t>{1, 2, 3}));
    }
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

    check_big_endian_decode_error<double>("d856480000000000000000", status_code::no_match_for_tag);
    check_big_endian_decode_error<double>("d8524700000000000000", status_code::unexpected_group_size);
    check_big_endian_decode_error<double>("d8525f", status_code::no_match_for_bstr_on_buffer);
    check_big_endian_owned_decode_error<double>("d8524700000000000000", status_code::unexpected_group_size);
    check_big_endian_non_contiguous_decode_error<double>("d8524700000000000000", status_code::unexpected_group_size);
}

TEST_CASE("rfc8746 typed array decode rejects truncated extended headers and large claimed payloads") {
    for (const auto *hex : {"d8", "d94e", "da00004e", "db0000000000004e"}) {
        check_decode_error<std::int32_t>(hex, status_code::incomplete);
    }

    for (const auto *hex : {"d84e58", "d84e5900", "d84e5a000000", "d84e5b00000000000000"}) {
        check_decode_error<std::int32_t>(hex, status_code::incomplete);
    }

    check_decode_error<std::int32_t>("d84e5affffffff", status_code::incomplete);
    if constexpr (std::numeric_limits<std::size_t>::max() < std::numeric_limits<std::uint64_t>::max()) {
        check_decode_error<std::int32_t>("d84e5b0000000100000000", status_code::error);
    } else {
        check_decode_error<std::int32_t>("d84e5b0000000100000000", status_code::incomplete);
    }
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
