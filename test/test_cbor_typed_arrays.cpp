#include "test_util.h"

#include <array>
#include <bit>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_typed_arrays.h>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <span>
#include <vector>

using namespace cbor::tags;

namespace {

template <typename T> std::vector<std::byte> encode_normal(std::span<const T> values) {
    std::vector<std::byte> output;
    auto                   enc = make_encoder(output);
    REQUIRE(enc(as_rfc8746_typed_array(values)));
    return output;
}

template <typename T> void check_roundtrip(const std::vector<T> &values) {
    const auto encoded = encode_normal(std::span<const T>{values});
    const auto decoded = decode_rfc8746_typed_array_view<T>(std::span<const std::byte>{encoded});

    REQUIRE(decoded);
    CHECK_EQ(decoded->payload_bytes().size(), values.size() * sizeof(T));
    CHECK_EQ(decoded->size(), values.size());
    CHECK_EQ(decoded->copy_values(), values);
}

template <typename T> void check_decode_error(const char *hex, status_code expected) {
    const auto bytes  = to_bytes(hex);
    const auto result = decode_rfc8746_typed_array_view<T>(std::span<const std::byte>{bytes});

    CAPTURE(hex);
    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), expected);
}

} // namespace

TEST_CASE("rfc8746 typed arrays encode and decode supported element types") {
    check_roundtrip<std::int32_t>({-1, 0, 1, 123456});
    check_roundtrip<std::int64_t>({-1, 0, 1, 0x0102030405060708LL});
    check_roundtrip<double>({-2.5, 0.0, 3.25});
}

TEST_CASE("rfc8746 int32 typed array uses exact little-endian wire bytes") {
    const std::vector<std::int32_t> values{-1, -2, 0x01020304};

    const auto encoded = encode_normal(std::span<const std::int32_t>{values});

    CHECK_EQ(to_hex(encoded), "d84e4cfffffffffeffffff04030201");
}

TEST_CASE("rfc8746 int64 and double typed arrays use exact little-endian wire bytes") {
    {
        const std::vector<std::int64_t> values{-1, 0x0102030405060708LL};

        const auto encoded = encode_normal(std::span<const std::int64_t>{values});

        CHECK_EQ(to_hex(encoded), "d84f50ffffffffffffffff0807060504030201");
    }

    {
        const std::vector<double> values{1.0, -2.5};

        const auto encoded = encode_normal(std::span<const double>{values});

        CHECK_EQ(to_hex(encoded), "d85650000000000000f03f00000000000004c0");
    }
}

TEST_CASE("rfc8746 typed array normal encoding matches flattened segments") {
    const std::vector<std::int32_t> values{-5, 42, 1000};
    const auto                      span   = std::span<const std::int32_t>{values};
    const auto                      normal = encode_normal(span);
    cbor_segments                   segments;

    if constexpr (std::endian::native == std::endian::little) {
        segments = encode_segments(as_rfc8746_typed_array(span));
    } else {
        segments = encode_rfc8746_typed_array_segments_copy(span);
    }

    CHECK_EQ(to_hex(normal), to_hex(flatten_segments(segments)));
}

TEST_CASE("rfc8746 typed array owned segment fallback matches normal encoding") {
    const std::vector<std::int64_t> values{-1, 0x0102030405060708LL};
    const auto                      span     = std::span<const std::int64_t>{values};
    const auto                      normal   = encode_normal(span);
    const auto                      segments = encode_rfc8746_typed_array_segments_copy(span);

    REQUIRE_EQ(segments.size(), 3U);
    CHECK(segments[0].is_owned());
    CHECK(segments[1].is_owned());
    CHECK(segments[2].is_owned());
    CHECK_EQ(to_hex(flatten_segments(segments)), to_hex(normal));
}

TEST_CASE("rfc8746 typed array segmented output borrows native little-endian payload") {
    if constexpr (std::endian::native == std::endian::little) {
        const std::vector<std::int32_t> values{1, -2, 3};
        const auto                      span           = std::span<const std::int32_t>{values};
        const auto                      original_bytes = std::as_bytes(span);

        const auto segments = encode_rfc8746_typed_array_segments(span);

        REQUIRE_EQ(segments.size(), 3U);
        CHECK(segments[0].is_owned());
        CHECK(segments[1].is_owned());
        CHECK(segments[2].is_borrowed());
        CHECK_EQ(segments[2].data(), original_bytes.data());
        CHECK_EQ(segments[2].size(), original_bytes.size());
    }
}

TEST_CASE("rfc8746 typed array view exposes aligned native span when safe") {
    if constexpr (std::endian::native == std::endian::little) {
        const std::vector<std::int32_t> values{1, -2, 3};
        const auto                      payload = std::as_bytes(std::span<const std::int32_t>{values});
        const auto                      view    = rfc8746_typed_array_view<std::int32_t>{payload};

        auto native = view.aligned_native_span();

        REQUIRE(native.has_value());
        CHECK_EQ(native->data(), values.data());
        CHECK_EQ(std::vector<std::int32_t>{native->begin(), native->end()}, values);
    }
}

TEST_CASE("rfc8746 typed array view rejects unaligned native span but still copies values") {
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

    const auto view = rfc8746_typed_array_view<std::int32_t>{std::span<const std::byte>{storage.data() + offset, sizeof(std::int32_t)}};

    CHECK_FALSE(view.aligned_native_span().has_value());
    CHECK_EQ(view.copy_values(), std::vector<std::int32_t>{0x04030201});
}

TEST_CASE("rfc8746 typed array decode rejects malformed inputs") {
    {
        const auto bytes  = to_bytes("d84f4400000000");
        const auto result = decode_rfc8746_typed_array_view<std::int32_t>(std::span<const std::byte>{bytes});
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::no_match_for_tag);
    }

    {
        const auto bytes  = to_bytes("d84e44010203");
        const auto result = decode_rfc8746_typed_array_view<std::int32_t>(std::span<const std::byte>{bytes});
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
    }

    {
        const auto bytes  = to_bytes("d84e43010203");
        const auto result = decode_rfc8746_typed_array_view<std::int32_t>(std::span<const std::byte>{bytes});
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::unexpected_group_size);
    }

    {
        const auto bytes  = to_bytes("d84e440102030400");
        const auto result = decode_rfc8746_typed_array_view<std::int32_t>(std::span<const std::byte>{bytes});
        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
    }
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

TEST_CASE("rfc8746 typed array decode currently accepts non-minimal integer headers") {
    const auto bytes  = to_bytes("d9004e580401020304");
    const auto result = decode_rfc8746_typed_array_view<std::int32_t>(std::span<const std::byte>{bytes});

    REQUIRE(result);
    CHECK_EQ(result->size(), 1U);
    CHECK_EQ(result->copy_values(), std::vector<std::int32_t>{0x04030201});
}
