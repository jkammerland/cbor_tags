#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_segments.h>
#include <cstddef>
#include <doctest/doctest.h>
#include <list>
#include <span>
#include <stdexcept>
#include <vector>

using namespace cbor::tags;

namespace {

template <typename RawView>
concept CanEncodeBorrowedSegments = requires(const RawView &view) { encode_encoded_segments(view); };

std::vector<std::byte> encode_normal_bstr(std::span<const std::byte> payload) {
    std::vector<std::byte> output;
    auto                   enc = make_encoder(output);
    REQUIRE(enc(payload));
    return output;
}

} // namespace

TEST_CASE("bstr segmented output flattens like normal encode and borrows payload") {
    auto payload = to_bytes("01020304");
    auto span    = std::span<const std::byte>{payload};

    const auto segments = encode_bstr_segments(span);
    const auto flat     = flatten_segments(segments);

    CHECK_EQ(to_hex(flat), to_hex(encode_normal_bstr(span)));
    REQUIRE_EQ(segments.size(), 2U);
    CHECK(segments[0].is_owned());
    CHECK_EQ(segments[0].kind(), cbor_segment_kind::owned);
    CHECK_EQ(to_hex(segments[0].bytes()), "44");
    CHECK(segments[1].is_borrowed());
    CHECK_EQ(segments[1].kind(), cbor_segment_kind::borrowed);
    CHECK_EQ(segments[1].data(), payload.data());
    CHECK_EQ(segments[1].size(), payload.size());
}

TEST_CASE("tagged bstr segmented output preserves CBOR bytes and borrows payload") {
    auto payload = to_bytes("aabbcc");
    auto span    = std::span<const std::byte>{payload};

    const auto dynamic_segments = encode_tagged_bstr_segments(dynamic_tag<std::uint64_t>{24}, span);
    const auto static_segments  = encode_tagged_bstr_segments(static_tag<24>{}, span);

    CHECK_EQ(to_hex(flatten_segments(dynamic_segments)), "d81843aabbcc");
    CHECK_EQ(to_hex(flatten_segments(static_segments)), "d81843aabbcc");
    REQUIRE_EQ(dynamic_segments.size(), 3U);
    CHECK(dynamic_segments[0].is_owned());
    CHECK(dynamic_segments[1].is_owned());
    CHECK(dynamic_segments[2].is_borrowed());
    CHECK_EQ(dynamic_segments[2].data(), payload.data());
}

TEST_CASE("indefinite bstr segmented output preserves chunks and borrows payload slices") {
    auto payload = to_bytes("0102030405");
    auto span    = std::span<const std::byte>{payload};

    const auto segments = encode_indefinite_bstr_segments(span, 3);

    REQUIRE_EQ(segments.size(), 6U);
    CHECK(segments[0].is_owned());
    CHECK_EQ(to_hex(segments[0].bytes()), "5f");
    CHECK(segments[1].is_owned());
    CHECK_EQ(to_hex(segments[1].bytes()), "43");
    CHECK(segments[2].is_borrowed());
    CHECK_EQ(segments[2].data(), payload.data());
    CHECK_EQ(segments[2].size(), 3U);
    CHECK(segments[3].is_owned());
    CHECK_EQ(to_hex(segments[3].bytes()), "42");
    CHECK(segments[4].is_borrowed());
    CHECK_EQ(segments[4].data(), payload.data() + 3);
    CHECK_EQ(segments[4].size(), 2U);
    CHECK(segments[5].is_owned());
    CHECK_EQ(to_hex(segments[5].bytes()), "ff");
    CHECK_EQ(to_hex(flatten_segments(segments)), "5f43010203420405ff");
}

TEST_CASE("indefinite bstr segmented output rejects zero chunk size") {
    auto payload = to_bytes("0102");
    auto span    = std::span<const std::byte>{payload};

    CHECK_THROWS_AS((void)encode_indefinite_bstr_segments(span, 0), std::invalid_argument);
}

TEST_CASE("span-backed raw encoded views become one borrowed segment without normalization") {
    {
        auto bytes      = to_bytes("820102");
        auto dec        = make_decoder(bytes);
        using item_view = typename decltype(dec)::raw_encoded_item_view;

        item_view item;
        REQUIRE(dec(item));

        const auto segments = encode_encoded_segments(item);
        REQUIRE_EQ(segments.size(), 1U);
        CHECK(segments[0].is_borrowed());
        CHECK_EQ(segments[0].data(), bytes.data());
        CHECK_EQ(to_hex(flatten_segments(segments)), "820102");
    }

    {
        auto bytes = to_bytes("9f018202039f0405ffff");
        auto dec   = make_decoder(bytes);

        encoded_array_view array;
        REQUIRE(dec(array));

        const auto segments = encode_encoded_segments(array);
        const auto flat     = flatten_segments(segments);

        REQUIRE_EQ(segments.size(), 1U);
        CHECK(segments[0].is_borrowed());
        CHECK_EQ(segments[0].data(), bytes.data());
        CHECK_EQ(to_hex(flat), "9f018202039f0405ffff");
    }

    {
        auto bytes     = to_bytes("a10102");
        auto dec       = make_decoder(bytes);
        using map_view = typename decltype(dec)::raw_encoded_map_view;

        map_view map;
        REQUIRE(dec(map));

        const auto segments = encode_encoded_segments(map);
        REQUIRE_EQ(segments.size(), 1U);
        CHECK(segments[0].is_borrowed());
        CHECK_EQ(segments[0].data(), bytes.data());
        CHECK_EQ(to_hex(flatten_segments(segments)), "a10102");
    }
}

TEST_CASE("non-contiguous raw encoded views use explicit owned segment copy fallback") {
    auto                 contiguous = to_bytes("9f01820203ff");
    std::list<std::byte> bytes(contiguous.begin(), contiguous.end());
    auto                 dec = make_decoder(bytes);
    using array_view         = typename decltype(dec)::raw_encoded_array_view;

    static_assert(!CanEncodeBorrowedSegments<array_view>);

    array_view array;
    REQUIRE(dec(array));
    CHECK_FALSE(array.span().has_value());

    const auto segments = encode_encoded_segments_copy(array);
    const auto flat     = flatten_segments(segments);

    REQUIRE_EQ(segments.size(), 1U);
    CHECK(segments[0].is_owned());
    CHECK_EQ(to_hex(flat), "9f01820203ff");
}
