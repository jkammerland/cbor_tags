#include "test_util.h"

#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_segments.h>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <list>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace cbor::tags;

namespace {

template <typename RawView>
concept CanEncodeBorrowedSegments = requires(const RawView &view) { encode_encoded_segments(view); };

template <typename RawView>
concept CanVisitBorrowedSegments = requires(const RawView &view) { visit_encoded_segments(view, [](std::span<const std::byte>) {}); };

std::vector<std::byte> encode_normal_bstr(std::span<const std::byte> payload) {
    std::vector<std::byte> output;
    auto                   enc = make_encoder(output);
    REQUIRE(enc(payload));
    return output;
}

template <typename VisitSegments> std::vector<std::byte> flatten_visited_segments(VisitSegments &&visit_segments) {
    std::vector<std::byte> output;
    visit_segments([&](std::span<const std::byte> segment) { output.insert(output.end(), segment.begin(), segment.end()); });
    return output;
}

void check_segment_header(std::uint64_t value, std::byte major, const char *expected_hex) {
    const auto header = cbor::tags::detail::encode_cbor_major_argument_header(value, major);
    CHECK_EQ(to_hex(header.span()), expected_hex);
}

} // namespace

TEST_CASE("segment header encoding covers CBOR size boundaries") {
    struct header_case {
        std::uint64_t value;
        const char   *bstr_hex;
        const char   *tag_hex;
    };

    constexpr std::array cases{
        header_case{0, "40", "c0"},
        header_case{23, "57", "d7"},
        header_case{24, "5818", "d818"},
        header_case{255, "58ff", "d8ff"},
        header_case{256, "590100", "d90100"},
        header_case{65535, "59ffff", "d9ffff"},
        header_case{65536, "5a00010000", "da00010000"},
        header_case{0xFFFFFFFFULL, "5affffffff", "daffffffff"},
        header_case{0x100000000ULL, "5b0000000100000000", "db0000000100000000"},
    };

    for (const auto &test_case : cases) {
        CAPTURE(test_case.value);
        check_segment_header(test_case.value, std::byte{0x40}, test_case.bstr_hex);
        check_segment_header(test_case.value, std::byte{0xC0}, test_case.tag_hex);
    }
}

TEST_CASE("bstr segmented output flattens like normal encode and borrows payload") {
    auto payload = to_bytes("01020304");
    auto span    = std::span<const std::byte>{payload};

    std::vector<std::byte> appended;
    append_bstr_segments(appended, span);
    CHECK_EQ(to_hex(appended), to_hex(encode_normal_bstr(span)));

    const auto visited = flatten_visited_segments(
        [&](auto &&visit_segment) { visit_bstr_segments(span, std::forward<decltype(visit_segment)>(visit_segment)); });
    CHECK_EQ(to_hex(visited), to_hex(appended));

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

TEST_CASE("empty bstr segmented output preserves definite and indefinite headers") {
    const std::span<const std::byte> payload;

    {
        const auto segments = encode_bstr_segments(payload);

        REQUIRE_EQ(segments.size(), 2U);
        CHECK(segments[0].is_owned());
        CHECK(segments[1].is_borrowed());
        CHECK(segments[1].empty());
        CHECK_EQ(to_hex(flatten_segments(segments)), "40");
    }

    {
        std::vector<std::byte> appended;
        append_indefinite_bstr_segments(appended, payload, 4);
        CHECK_EQ(to_hex(appended), "5fff");

        const auto segments = encode_indefinite_bstr_segments(payload, 4);

        REQUIRE_EQ(segments.size(), 2U);
        CHECK(segments[0].is_owned());
        CHECK(segments[1].is_owned());
        CHECK_EQ(to_hex(flatten_segments(segments)), "5fff");
    }
}

TEST_CASE("tagged bstr segmented output preserves CBOR bytes and borrows payload") {
    auto payload = to_bytes("aabbcc");
    auto span    = std::span<const std::byte>{payload};

    const auto dynamic_segments = encode_tagged_bstr_segments(dynamic_tag<std::uint64_t>{24}, span);
    const auto static_segments  = encode_tagged_bstr_segments(static_tag<24>{}, span);

    std::vector<std::byte> dynamic_appended;
    std::vector<std::byte> static_appended;
    append_tagged_bstr_segments(dynamic_appended, dynamic_tag<std::uint64_t>{24}, span);
    append_tagged_bstr_segments(static_appended, static_tag<24>{}, span);

    CHECK_EQ(to_hex(dynamic_appended), "d81843aabbcc");
    CHECK_EQ(to_hex(static_appended), "d81843aabbcc");
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

    std::vector<std::byte> appended;
    append_indefinite_bstr_segments(appended, span, 3);
    CHECK_EQ(to_hex(appended), "5f43010203420405ff");

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

TEST_CASE("owned and borrowed cbor segments keep their lifetime contracts") {
    auto short_payload = to_bytes("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    auto long_payload  = to_bytes("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20");

    REQUIRE_EQ(short_payload.size(), cbor_segment::inline_owned_capacity);
    REQUIRE_EQ(long_payload.size(), cbor_segment::inline_owned_capacity + 1U);

    auto short_owned = cbor_segment::owned(std::span<const std::byte>{short_payload});
    auto long_owned  = cbor_segment::owned(std::span<const std::byte>{long_payload});
    auto borrowed    = cbor_segment::borrowed(std::span<const std::byte>{short_payload});

    short_payload[0] = std::byte{0xFF};
    long_payload[0]  = std::byte{0xEE};

    CHECK(short_owned.is_owned());
    CHECK(long_owned.is_owned());
    CHECK(borrowed.is_borrowed());
    CHECK_EQ(to_hex(short_owned.bytes()), "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    CHECK_EQ(to_hex(long_owned.bytes()), "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20");
    CHECK_EQ(to_hex(borrowed.bytes()), "ff0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
}

TEST_CASE("indefinite bstr segmented output rejects zero chunk size") {
    auto payload = to_bytes("0102");
    auto span    = std::span<const std::byte>{payload};

    CHECK_THROWS_AS(append_indefinite_bstr_segments(payload, span, 0), std::invalid_argument);
    CHECK_THROWS_AS((void)encode_indefinite_bstr_segments(span, 0), std::invalid_argument);
}

TEST_CASE("span-backed raw encoded views become one borrowed segment without normalization") {
    {
        auto bytes      = to_bytes("820102");
        auto dec        = make_decoder(bytes);
        using item_view = typename decltype(dec)::raw_encoded_item_view;

        item_view item;
        REQUIRE(dec(item));

        static_assert(CanVisitBorrowedSegments<item_view>);
        std::vector<std::byte> appended;
        append_encoded_segments(appended, item);
        CHECK_EQ(to_hex(appended), "820102");

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

    {
        auto bytes = to_bytes("f5820102f4");
        auto dec   = make_decoder(bytes);

        bool               prefix{};
        encoded_array_view array;
        bool               suffix{};
        REQUIRE(dec(prefix, array, suffix));

        const auto segments = encode_encoded_segments(array);
        REQUIRE_EQ(segments.size(), 1U);
        CHECK(segments[0].is_borrowed());
        CHECK_EQ(segments[0].data(), bytes.data() + 1);
        CHECK_EQ(to_hex(flatten_segments(segments)), "820102");
    }
}

TEST_CASE("non-contiguous raw encoded views use explicit owned segment copy fallback") {
    auto                 contiguous = to_bytes("9f01820203ff");
    std::list<std::byte> bytes(contiguous.begin(), contiguous.end());
    auto                 dec = make_decoder(bytes);
    using array_view         = typename decltype(dec)::raw_encoded_array_view;

    static_assert(!CanEncodeBorrowedSegments<array_view>);
    static_assert(!CanVisitBorrowedSegments<array_view>);

    array_view array;
    REQUIRE(dec(array));

    std::vector<std::byte> appended;
    append_encoded_segments(appended, array);
    CHECK_EQ(to_hex(appended), "9f01820203ff");

    const auto segments = encode_encoded_segments_copy(array);
    const auto flat     = flatten_segments(segments);

    REQUIRE_EQ(segments.size(), 1U);
    CHECK(segments[0].is_owned());
    CHECK_EQ(to_hex(flat), "9f01820203ff");
}
