#include "test_util.h"

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
