#include "test_util.h"

#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_segments.h>
#include <cbor_tags/extensions/rfc8746_typed_arrays.h>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <list>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace cbor::tags;
using cbor::tags::test::detail::allocation_failure_guard;

namespace {

template <typename RawView>
concept CanEncodeSegments = requires(const RawView &view) { to_segments(view); };

template <typename RawView>
concept CanBorrowSegments = requires(const RawView &view) { borrow_segments(view); };

template <typename RawView>
concept CanBorrowSegmentsRvalue = requires { borrow_segments(std::declval<RawView>()); };

template <typename RawView>
concept CanBorrowSegmentsExplicitRvalue = requires { borrow_segments<RawView>(std::declval<std::remove_reference_t<RawView>>()); };

template <typename RawView>
concept CanEncodeEncodedSegments = requires(const RawView &view) { encode_encoded_segments(view); };

template <typename RawView>
concept CanEncodeEncodedSegmentsRvalue = requires { encode_encoded_segments(std::declval<RawView>()); };

template <typename RawView>
concept CanEncodeEncodedSegmentsExplicitRvalue =
    requires { encode_encoded_segments<RawView>(std::declval<std::remove_reference_t<RawView>>()); };

template <typename Segments, typename Output>
concept CanFlattenTo = requires(const Segments &segments, Output &output) { segments.flatten_to(output); };

template <typename RawView>
concept CanVisitBorrowedSegments = requires(const RawView &view) { visit_encoded_segments(view, [](std::span<const std::byte>) {}); };

template <typename RawView>
concept CanVisitBorrowedSegmentsRvalue = requires { visit_encoded_segments(std::declval<RawView>(), [](std::span<const std::byte>) {}); };

template <typename RawView>
concept CanVisitBorrowedSegmentsExplicitRvalue =
    requires { visit_encoded_segments<RawView>(std::declval<std::remove_reference_t<RawView>>(), [](std::span<const std::byte>) {}); };

template <typename Storage>
concept CanBuildByteSegments = requires { typename basic_byte_segments<Storage>; };

template <typename T>
concept CanWrapSegmentItemAsBstr = requires(T &&value) { as_bstr(std::forward<T>(value)); };

struct copyable_owning_byte_view : std::ranges::view_interface<copyable_owning_byte_view> {
    std::vector<std::byte> bytes;

    copyable_owning_byte_view() = default;
    explicit copyable_owning_byte_view(std::vector<std::byte> input) : bytes(std::move(input)) {}

    [[nodiscard]] auto begin() const noexcept { return bytes.begin(); }
    [[nodiscard]] auto end() const noexcept { return bytes.end(); }
    [[nodiscard]] auto data() const noexcept { return bytes.data(); }
    [[nodiscard]] auto size() const noexcept { return bytes.size(); }
};

using owning_encoded_item_view = basic_encoded_item_view<copyable_owning_byte_view>;

struct tracking_byte_segment_storage {
    using segment_type   = basic_byte_segment<4>;
    using container_type = std::vector<segment_type>;

    int         *reserve_owned_calls{};
    std::size_t *last_owned_reserve{};

    [[nodiscard]] container_type make_container() const { return {}; }
    [[nodiscard]] segment_type   make_owned(std::span<const std::byte> bytes) const { return segment_type::owned(bytes); }
    [[nodiscard]] segment_type   make_borrowed(std::span<const std::byte> bytes) const { return segment_type::borrowed(bytes); }

    void reserve_owned_bytes(std::size_t count) {
        if (reserve_owned_calls != nullptr) {
            ++(*reserve_owned_calls);
        }
        if (last_owned_reserve != nullptr) {
            *last_owned_reserve = count;
        }
    }
};

struct declining_try_append_byte_segment_storage {
    using segment_type   = basic_byte_segment<4>;
    using container_type = std::vector<segment_type>;

    int *try_calls{};

    [[nodiscard]] container_type make_container() const { return {}; }
    [[nodiscard]] segment_type   make_owned(std::span<const std::byte> bytes) const { return segment_type::owned(bytes); }
    [[nodiscard]] segment_type   make_borrowed(std::span<const std::byte> bytes) const { return segment_type::borrowed(bytes); }

    bool try_append_owned(container_type &, std::span<const std::byte>) {
        if (try_calls != nullptr) {
            ++(*try_calls);
        }
        return false;
    }
};

struct invalid_byte_segment_storage {
    using segment_type   = basic_byte_segment<4>;
    using container_type = std::vector<segment_type>;

    [[nodiscard]] container_type make_container() const { return {}; }
    [[nodiscard]] segment_type   make_owned(std::span<const std::byte> bytes) const { return segment_type::owned(bytes); }
};

struct non_coalescing_byte_segment {
    [[nodiscard]] static non_coalescing_byte_segment owned(std::span<const std::byte> bytes) {
        non_coalescing_byte_segment segment;
        segment.kind_ = byte_segment_kind::owned;
        segment.owned_.assign(bytes.begin(), bytes.end());
        return segment;
    }

    [[nodiscard]] static non_coalescing_byte_segment borrowed(std::span<const std::byte> bytes) {
        non_coalescing_byte_segment segment;
        segment.kind_     = byte_segment_kind::borrowed;
        segment.borrowed_ = bytes;
        return segment;
    }

    [[nodiscard]] byte_segment_kind kind() const noexcept { return kind_; }
    [[nodiscard]] bool              is_owned() const noexcept { return kind_ == byte_segment_kind::owned; }
    [[nodiscard]] bool              is_borrowed() const noexcept { return kind_ == byte_segment_kind::borrowed; }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        if (is_borrowed()) {
            return borrowed_;
        }
        return std::span<const std::byte>{owned_.data(), owned_.size()};
    }

    [[nodiscard]] const std::byte *data() const noexcept { return bytes().data(); }
    [[nodiscard]] std::size_t      size() const noexcept { return bytes().size(); }
    [[nodiscard]] bool             empty() const noexcept { return bytes().empty(); }

  private:
    byte_segment_kind          kind_{byte_segment_kind::owned};
    std::vector<std::byte>     owned_{};
    std::span<const std::byte> borrowed_{};
};

struct non_coalescing_byte_segment_storage {
    using segment_type   = non_coalescing_byte_segment;
    using container_type = std::vector<segment_type>;

    [[nodiscard]] container_type make_container() const { return {}; }
    [[nodiscard]] segment_type   make_owned(std::span<const std::byte> bytes) const { return segment_type::owned(bytes); }
    [[nodiscard]] segment_type   make_borrowed(std::span<const std::byte> bytes) const { return segment_type::borrowed(bytes); }
};

static_assert(IsEncodedItemView<owning_encoded_item_view>);
static_assert(detail::SpanBackedEncodedItemView<owning_encoded_item_view>);
static_assert(!detail::BorrowedSpanBackedEncodedItemView<owning_encoded_item_view>);
static_assert(!detail::EncodedByteViewRange<std::ranges::owning_view<std::vector<std::byte>>>);

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
    const auto header = detail::encode_cbor_major_argument_header(value, major);
    CHECK_EQ(to_hex(header.span()), expected_hex);
}

template <typename Segments> bool has_borrowed_segment(const Segments &segments, const std::byte *data, std::size_t size) {
    for (const auto &segment : segments) {
        if (segment.is_borrowed() && segment.data() == data && segment.size() == size) {
            return true;
        }
    }
    return false;
}

int count_borrowed_segments(const cbor_segments &segments, const std::byte *data, std::size_t size) {
    int count{};
    for (const auto &segment : segments) {
        if (segment.is_borrowed() && segment.data() == data && segment.size() == size) {
            ++count;
        }
    }
    return count;
}

int count_borrowed_segments(const cbor_segments &segments) {
    int count{};
    for (const auto &segment : segments) {
        if (segment.is_borrowed()) {
            ++count;
        }
    }
    return count;
}

std::vector<std::byte> nested_definite_arrays(std::size_t depth) {
    std::vector<std::byte> bytes(depth, std::byte{0x81});
    bytes.push_back(std::byte{0x00});
    return bytes;
}

std::vector<std::byte> nested_indefinite_arrays(std::size_t depth) {
    std::vector<std::byte> bytes;
    bytes.reserve((depth * 2U) + 1U);
    for (std::size_t index = 0; index < depth; ++index) {
        bytes.push_back(std::byte{0x9F});
    }
    bytes.push_back(std::byte{0x00});
    for (std::size_t index = 0; index < depth; ++index) {
        bytes.push_back(std::byte{0xFF});
    }
    return bytes;
}

} // namespace

using tiny_inline_byte_segments   = basic_byte_segments<default_byte_segment_storage<4>>;
using tracking_byte_segments      = basic_byte_segments<tracking_byte_segment_storage>;
using split_byte_segments         = basic_byte_segments<non_coalescing_byte_segment_storage>;
using declining_try_byte_segments = basic_byte_segments<declining_try_append_byte_segment_storage>;

static_assert(std::same_as<byte_segment_kind, cbor_segment_kind>);
static_assert(std::same_as<byte_segment, cbor_segment>);
static_assert(std::same_as<byte_segments, cbor_segments>);
static_assert(CborOutputBuffer<byte_segments>);
static_assert(CborSegmentOutputBuffer<byte_segments>);
static_assert(!CborAppendOutputBuffer<byte_segments>);
static_assert(CborOutputBuffer<tiny_inline_byte_segments>);
static_assert(CborSegmentOutputBuffer<tiny_inline_byte_segments>);
static_assert(!CborAppendOutputBuffer<tiny_inline_byte_segments>);
static_assert(CborOutputBuffer<tracking_byte_segments>);
static_assert(CborSegmentOutputBuffer<tracking_byte_segments>);
static_assert(!CborAppendOutputBuffer<tracking_byte_segments>);
static_assert(CborOutputBuffer<split_byte_segments>);
static_assert(CborSegmentOutputBuffer<split_byte_segments>);
static_assert(!CborAppendOutputBuffer<split_byte_segments>);
static_assert(CborOutputBuffer<declining_try_byte_segments>);
static_assert(CborSegmentOutputBuffer<declining_try_byte_segments>);
static_assert(!CborAppendOutputBuffer<declining_try_byte_segments>);
static_assert(!CanBuildByteSegments<invalid_byte_segment_storage>);
static_assert(CborOutputBuffer<cbor_segments>);
static_assert(CborSegmentOutputBuffer<cbor_segments>);
static_assert(!CborAppendOutputBuffer<cbor_segments>);
static_assert(CanFlattenTo<cbor_segments, std::vector<std::byte>>);
static_assert(CanFlattenTo<cbor_segments, std::vector<unsigned char>>);
static_assert(!CanFlattenTo<cbor_segments, std::vector<std::uint16_t>>);
static_assert(!std::constructible_from<encoded_item_segments, cbor_segments>);
static_assert(!std::constructible_from<encoded_item_bstr, const encoded_item_segments &>);
static_assert(!std::constructible_from<encoded_item_bstr, encoded_item_segments &&>);
static_assert(CanWrapSegmentItemAsBstr<encoded_item_segments &>);
static_assert(CanWrapSegmentItemAsBstr<const encoded_item_segments &>);
static_assert(!CanWrapSegmentItemAsBstr<encoded_item_segments &&>);

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

TEST_CASE("cbor argument headers remain accepted as owned segment input") {
    const auto header = detail::encode_cbor_major_argument_header(24, std::byte{0x40});

    const auto segment = cbor_segment::owned(header);
    CHECK(segment.is_owned());
    CHECK_EQ(to_hex(segment.bytes()), "5818");

    cbor_segments segments;
    segments.append_owned(header);

    CHECK_EQ(to_hex(flatten_segments(segments)), "5818");
}

TEST_CASE("cbor segments can be used as the normal encoder output backend") {
    std::vector<int> ints{1, 2, 3};
    auto             blob0 = to_bytes("0102");
    auto             blob1 = to_bytes("aabbcc");
    std::array       blobs{std::span<const std::byte>{blob0}, std::span<const std::byte>{blob1}};

    auto make_bstrs = [&] { return blobs | std::views::transform([](std::span<const std::byte> blob) { return as_bstr_range(blob); }); };

    cbor_segments segmented;
    auto          segment_encoder = make_encoder(segmented);
    REQUIRE(segment_encoder(as_array{2}, as_array_range(ints), as_array_range(make_bstrs())));

    std::vector<std::byte> contiguous;
    auto                   contiguous_encoder = make_encoder(contiguous);
    REQUIRE(contiguous_encoder(as_array{2}, as_array_range(ints), as_array_range(make_bstrs())));

    CHECK_EQ(to_hex(segmented.flatten()), to_hex(contiguous));
    CHECK(has_borrowed_segment(segmented, blob0.data(), blob0.size()));
    CHECK(has_borrowed_segment(segmented, blob1.data(), blob1.size()));
}

TEST_CASE("byte segments support custom inline storage as encoder backend") {
    auto payload = to_bytes("010203040506");

    tiny_inline_byte_segments segmented;
    auto                      enc = make_encoder(segmented);
    REQUIRE(enc(as_bstr_range(std::span<const std::byte>{payload})));

    std::vector<std::byte> flattened;
    segmented.flatten_to(flattened);

    CHECK_EQ(to_hex(flattened), "46010203040506");
    CHECK(has_borrowed_segment(segmented, payload.data(), payload.size()));
}

TEST_CASE("byte segments do not require custom segment coalescing") {
    split_byte_segments segmented;
    auto                enc = make_encoder(segmented);

    REQUIRE(enc(as_array{2}, 1, 2));

    CHECK_EQ(to_hex(flatten_segments(segmented)), "820102");
    CHECK_GT(segmented.size(), 1U);
}

TEST_CASE("storage try append hook falls back when it declines") {
    int try_calls{};

    declining_try_byte_segments segmented{declining_try_append_byte_segment_storage{.try_calls = &try_calls}};
    auto                        enc = make_encoder(segmented);

    REQUIRE(enc(as_array{2}, 1, 2));

    CHECK_GT(try_calls, 0);
    CHECK_EQ(to_hex(flatten_segments(segmented)), "820102");
}

TEST_CASE("bstr segment helpers can append into custom segment storage") {
    auto payload = to_bytes("aabbccdd");

    tiny_inline_byte_segments segments;
    encode_tagged_bstr_segments_into(segments, static_tag<24>{}, std::span<const std::byte>{payload});

    CHECK_EQ(to_hex(flatten_segments(segments)), "d81844aabbccdd");
    CHECK(has_borrowed_segment(segments, payload.data(), payload.size()));
}

TEST_CASE("bstr segment into helpers append to existing segment buffers") {
    auto payload = to_bytes("aabbcc");
    auto span    = std::span<const std::byte>{payload};

    {
        cbor_segments segments;
        segments.append_owned({std::byte{0x00}});

        encode_bstr_segments_into(segments, span.first(2));

        CHECK_EQ(to_hex(flatten_segments(segments)), "0042aabb");
        CHECK(has_borrowed_segment(segments, payload.data(), 2U));
    }

    {
        cbor_segments segments;
        segments.append_owned({std::byte{0x00}});

        encode_indefinite_bstr_segments_into(segments, span, 2);

        CHECK_EQ(to_hex(flatten_segments(segments)), "005f42aabb41ccff");
        CHECK(has_borrowed_segment(segments, payload.data(), 2U));
        CHECK(has_borrowed_segment(segments, payload.data() + 2, 1U));
    }

    {
        cbor_segments segments;
        segments.append_owned({std::byte{0x00}});

        encode_tagged_bstr_segments_into(segments, std::uint64_t{24}, span.first(2));

        CHECK_EQ(to_hex(flatten_segments(segments)), "00d81842aabb");
        CHECK(has_borrowed_segment(segments, payload.data(), 2U));
    }

    {
        cbor_segments segments;
        segments.append_owned({std::byte{0x00}});

        encode_tagged_bstr_segments_into(segments, dynamic_tag<std::uint64_t>{24}, span.first(2));

        CHECK_EQ(to_hex(flatten_segments(segments)), "00d81842aabb");
        CHECK(has_borrowed_segment(segments, payload.data(), 2U));
    }
}

TEST_CASE("bstr segment into helpers reject zero chunk size") {
    auto          payload = to_bytes("0102");
    cbor_segments segments;

    CHECK_THROWS_AS(encode_indefinite_bstr_segments_into(segments, std::span<const std::byte>{payload}, 0), std::invalid_argument);
}

TEST_CASE("segment into helpers avoid allocation after segment reserve") {
    auto payload = to_bytes("aabbcc");
    auto span    = std::span<const std::byte>{payload};
    auto prefix  = to_bytes("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");

    {
        cbor_segments segments;
        segments.reserve_segments(2);

        {
            allocation_failure_guard guard;
            encode_bstr_segments_into(segments, span);
        }

        CHECK_EQ(to_hex(flatten_segments(segments)), "43aabbcc");
    }

    {
        cbor_segments segments;
        segments.reserve_segments(6);

        {
            allocation_failure_guard guard;
            encode_indefinite_bstr_segments_into(segments, span, 2);
        }

        CHECK_EQ(to_hex(flatten_segments(segments)), "5f42aabb41ccff");
    }

    {
        cbor_segments segments;
        segments.reserve_segments(3);

        {
            allocation_failure_guard guard;
            encode_tagged_bstr_segments_into(segments, static_tag<24>{}, span);
        }

        CHECK_EQ(to_hex(flatten_segments(segments)), "d81843aabbcc");
    }

    {
        cbor_segments segments;
        segments.reserve_segments(3);
        segments.append_owned(std::span<const std::byte>{prefix});

        {
            allocation_failure_guard guard;
            encode_bstr_segments_into(segments, span);
        }

        CHECK_EQ(to_hex(flatten_segments(segments)), to_hex(prefix) + "43aabbcc");
    }

    {
        cbor_segments segments;
        segments.reserve_segments(7);
        segments.append_owned(std::span<const std::byte>{prefix});

        {
            allocation_failure_guard guard;
            encode_indefinite_bstr_segments_into(segments, span, 2);
        }

        CHECK_EQ(to_hex(flatten_segments(segments)), to_hex(prefix) + "5f42aabb41ccff");
    }

    {
        cbor_segments segments;
        segments.reserve_segments(4);
        segments.append_owned(std::span<const std::byte>{prefix});

        {
            allocation_failure_guard guard;
            encode_tagged_bstr_segments_into(segments, static_tag<24>{}, span);
        }

        CHECK_EQ(to_hex(flatten_segments(segments)), to_hex(prefix) + "d81843aabbcc");
    }
}

TEST_CASE("byte segments forward owned-byte reserve to storage policy") {
    int         reserve_calls{};
    std::size_t last_reserve{};

    tracking_byte_segments segments{
        tracking_byte_segment_storage{.reserve_owned_calls = &reserve_calls, .last_owned_reserve = &last_reserve}};
    segments.reserve_owned_bytes(128);

    CHECK_EQ(reserve_calls, 1);
    CHECK_EQ(last_reserve, 128U);
}

TEST_CASE("segmented byte string range preserves header order for non-contiguous payloads") {
    std::list<std::byte> payload{std::byte{0x00}, std::byte{0xFF}, std::byte{0x10}, std::byte{0x20}};

    cbor_segments segmented;
    auto          enc = make_encoder(segmented);
    REQUIRE(enc(as_bstr_range(payload)));

    CHECK_EQ(to_hex(segmented.flatten()), "4400ff1020");
}

TEST_CASE("segmented text string range preserves header order for non-contiguous payloads") {
    std::list<char> payload{'t', 'e', 'x', 't'};

    cbor_segments segmented;
    auto          enc = make_encoder(segmented);
    REQUIRE(enc(as_tstr_range(payload)));

    CHECK_EQ(count_borrowed_segments(segmented), 0);
    CHECK_EQ(to_hex(segmented.flatten()), "6474657874");
}

TEST_CASE("direct segmented encode writes are visible without operator flush") {
    cbor_segments segmented;
    auto          enc = make_encoder(segmented);

    enc.encode(as_array{1});
    enc.encode(1);

    CHECK_EQ(to_hex(segmented.flatten()), "8101");
}

TEST_CASE("direct segmented byte string encode borrows span payload") {
    auto payload = to_bytes("01020304");

    cbor_segments segmented;
    auto          enc = make_encoder(segmented);
    REQUIRE(enc(std::span<const std::byte>{payload}));

    CHECK_EQ(to_hex(segmented.flatten()), "4401020304");
    CHECK(has_borrowed_segment(segmented, payload.data(), payload.size()));
}

TEST_CASE("direct segmented raw encoded view encode borrows span payload") {
    auto bytes = to_bytes("820102");
    auto raw   = encoded_item_view{std::span<const std::byte>{bytes}};

    cbor_segments segmented;
    auto          enc = make_encoder(segmented);
    REQUIRE(enc(raw));

    CHECK_EQ(to_hex(segmented.flatten()), "820102");
    CHECK(has_borrowed_segment(segmented, bytes.data(), bytes.size()));
}

TEST_CASE("encoded item segments can be array elements map values tags and bstr payloads") {
    auto payload = to_bytes("010203");

    auto item_result = encode_item_segments(as_bstr_range(std::span<const std::byte>{payload}));
    REQUIRE(item_result);
    auto item = std::move(item_result).value();

    cbor_segments segmented;
    auto          segment_encoder = make_encoder(segmented);
    REQUIRE(
        segment_encoder(as_array{4}, item, make_tag_pair(static_tag<100>{}, item), as_bstr(item), as_map{1}, std::string{"payload"}, item));

    const auto item_bytes = item.flatten();
    auto       item_view  = encoded_item_view{std::span<const std::byte>{item_bytes}};

    std::vector<std::byte> contiguous;
    auto                   contiguous_encoder = make_encoder(contiguous);
    REQUIRE(contiguous_encoder(as_array{4}, item_view, make_tag_pair(static_tag<100>{}, item_view), std::span<const std::byte>{item_bytes},
                               as_map{1}, std::string{"payload"}, item_view));

    CHECK_EQ(to_hex(segmented.flatten()), to_hex(contiguous));
    CHECK_EQ(count_borrowed_segments(segmented, payload.data(), payload.size()), 4);

    std::vector<std::byte> appendable;
    auto                   appendable_encoder = make_encoder(appendable);
    REQUIRE(appendable_encoder(as_array{3}, item, make_tag_pair(static_tag<100>{}, item), as_bstr(item)));

    CHECK_EQ(to_hex(appendable), "8343010203d864430102034443010203");
}

TEST_CASE("encoded item segment validation requires exactly one complete cbor item") {
    {
        cbor_segments segments;
        segments.append_owned({std::byte{0x01}});

        auto item = validate_item_segments(std::move(segments));

        REQUIRE(item);
        CHECK_EQ(to_hex(item->flatten()), "01");
    }

    {
        cbor_segments segments;
        segments.append_owned({std::byte{0x01}, std::byte{0x02}});

        auto item = validate_item_segments(std::move(segments));

        REQUIRE_FALSE(item);
        CHECK_EQ(item.error(), status_code::error);
    }

    {
        cbor_segments segments;
        segments.append_owned({std::byte{0x58}});

        auto item = validate_item_segments(std::move(segments));

        REQUIRE_FALSE(item);
        CHECK_EQ(item.error(), status_code::incomplete);
    }
}

TEST_CASE("encoded item segment validation skips deep definite containers without frame depth pressure") {
    const auto bytes = nested_definite_arrays(1024);

    cbor_segments segments;
    segments.append_owned(std::span<const std::byte>{bytes.data(), bytes.size()});

    auto item = validate_item_segments(std::move(segments));

    REQUIRE(item);
    CHECK_EQ(to_hex(item->flatten()), to_hex(bytes));
}

TEST_CASE("encoded item segment validation keeps indefinite traversal state bounded internally") {
    {
        const auto bytes = nested_indefinite_arrays(256);

        cbor_segments segments;
        segments.append_owned(std::span<const std::byte>{bytes.data(), bytes.size()});

        auto item = validate_item_segments(std::move(segments));

        REQUIRE(item);
        CHECK_EQ(to_hex(item->flatten()), to_hex(bytes));
    }

    {
        const auto bytes = nested_indefinite_arrays(257);

        cbor_segments segments;
        segments.append_owned(std::span<const std::byte>{bytes.data(), bytes.size()});

        auto item = validate_item_segments(std::move(segments));

        REQUIRE_FALSE(item);
        CHECK_EQ(item.error(), status_code::error);
    }
}

TEST_CASE("typed array codec borrows payload when the encoder output is segmented") {
    using namespace cbor::tags::ext::rfc8746;

    std::vector<std::int32_t> values{1, -2, 3};

    cbor_segments segmented;
    auto          segment_encoder = make_encoder<typed_array_codec>(segmented);
    REQUIRE(segment_encoder(as_typed_array(values)));

    std::vector<std::byte> contiguous;
    auto                   contiguous_encoder = make_encoder<typed_array_codec>(contiguous);
    REQUIRE(contiguous_encoder(as_typed_array(values)));

    const auto payload = std::as_bytes(std::span<const std::int32_t>{values});
    CHECK_EQ(to_hex(segmented.flatten()), to_hex(contiguous));
    CHECK(has_borrowed_segment(segmented, payload.data(), payload.size()));
}

TEST_CASE("owning typed array codec copies payload when the encoder output is segmented") {
    using namespace cbor::tags::ext::rfc8746;

    auto array = typed_array<std::int32_t>{{1, -2, 3}};

    cbor_segments segmented;
    auto          segment_encoder = make_encoder<typed_array_codec>(segmented);
    REQUIRE(segment_encoder(array));

    std::vector<std::byte> contiguous;
    auto                   contiguous_encoder = make_encoder<typed_array_codec>(contiguous);
    REQUIRE(contiguous_encoder(array));

    const auto payload = std::as_bytes(array.span());
    CHECK_EQ(to_hex(segmented.flatten()), to_hex(contiguous));
    CHECK_FALSE(has_borrowed_segment(segmented, payload.data(), payload.size()));
}

TEST_CASE("segmented bstr range borrowing is limited to explicit borrowed ranges") {
    auto bytes = to_bytes("01020304");

    {
        cbor_segments segmented;
        auto          enc = make_encoder(segmented);
        REQUIRE(enc(as_bstr_range(bytes)));

        CHECK(has_borrowed_segment(segmented, bytes.data(), bytes.size()));
        CHECK_EQ(to_hex(segmented.flatten()), "4401020304");
    }

    {
        cbor_segments segmented;
        auto          enc = make_encoder(segmented);
        REQUIRE(enc(as_bstr_range(to_bytes("01020304"))));

        CHECK_EQ(count_borrowed_segments(segmented), 0);
        CHECK_EQ(to_hex(segmented.flatten()), "4401020304");
    }
}

TEST_CASE("segmented tstr range borrowing is limited to explicit borrowed ranges") {
    std::string text{"text"};
    const auto  text_bytes = std::as_bytes(std::span{text.data(), text.size()});

    {
        cbor_segments segmented;
        auto          enc = make_encoder(segmented);
        REQUIRE(enc(as_tstr_range(text)));

        CHECK(has_borrowed_segment(segmented, text_bytes.data(), text_bytes.size()));
        CHECK_EQ(to_hex(segmented.flatten()), "6474657874");
    }

    {
        cbor_segments segmented;
        auto          enc = make_encoder(segmented);
        REQUIRE(enc(as_tstr_range(std::string{"text"})));

        CHECK_EQ(count_borrowed_segments(segmented), 0);
        CHECK_EQ(to_hex(segmented.flatten()), "6474657874");
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

        REQUIRE_EQ(segments.size(), 1U);
        CHECK(segments[0].is_owned());
        CHECK_EQ(to_hex(segments[0].bytes()), "5fff");
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
    REQUIRE_EQ(dynamic_segments.size(), 2U);
    CHECK(dynamic_segments[0].is_owned());
    CHECK_EQ(to_hex(dynamic_segments[0].bytes()), "d81843");
    CHECK(dynamic_segments[1].is_borrowed());
    CHECK_EQ(dynamic_segments[1].data(), payload.data());
}

TEST_CASE("indefinite bstr segmented output preserves chunks and borrows payload slices") {
    auto payload = to_bytes("0102030405");
    auto span    = std::span<const std::byte>{payload};

    const auto segments = encode_indefinite_bstr_segments(span, 3);

    std::vector<std::byte> appended;
    append_indefinite_bstr_segments(appended, span, 3);
    CHECK_EQ(to_hex(appended), "5f43010203420405ff");

    REQUIRE_EQ(segments.size(), 5U);
    CHECK(segments[0].is_owned());
    CHECK_EQ(to_hex(segments[0].bytes()), "5f43");
    CHECK(segments[1].is_borrowed());
    CHECK_EQ(segments[1].data(), payload.data());
    CHECK_EQ(segments[1].size(), 3U);
    CHECK(segments[2].is_owned());
    CHECK_EQ(to_hex(segments[2].bytes()), "42");
    CHECK(segments[3].is_borrowed());
    CHECK_EQ(segments[3].data(), payload.data() + 3);
    CHECK_EQ(segments[3].size(), 2U);
    CHECK(segments[4].is_owned());
    CHECK_EQ(to_hex(segments[4].bytes()), "ff");
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

    auto coalesced = cbor_segment::owned(std::span<const std::byte>{short_payload});
    auto byte_20   = to_bytes("20");
    auto byte_21   = to_bytes("21");

    REQUIRE(coalesced.append_owned(std::span<const std::byte>{byte_20}));
    REQUIRE(coalesced.append_owned(std::span<const std::byte>{byte_21}));

    byte_20[0] = std::byte{0xAA};
    byte_21[0] = std::byte{0xBB};

    CHECK(coalesced.is_owned());
    CHECK_EQ(to_hex(coalesced.bytes()), "ff0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f2021");
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
        static_assert(!CanVisitBorrowedSegmentsRvalue<item_view>);
        static_assert(!CanVisitBorrowedSegmentsExplicitRvalue<const item_view &>);
        static_assert(CanBorrowSegments<item_view>);
        static_assert(!CanBorrowSegmentsRvalue<item_view>);
        static_assert(!CanBorrowSegmentsExplicitRvalue<const item_view &>);
        static_assert(CanEncodeEncodedSegments<item_view>);
        static_assert(!CanEncodeEncodedSegmentsRvalue<item_view>);
        static_assert(!CanEncodeEncodedSegmentsExplicitRvalue<const item_view &>);
        std::vector<std::byte> appended;
        append_encoded_segments(appended, item);
        CHECK_EQ(to_hex(appended), "820102");

        const auto segments = to_segments(item);
        REQUIRE_EQ(segments.size(), 1U);
        CHECK(segments[0].is_owned());
        CHECK_EQ(to_hex(flatten_segments(segments)), "820102");

        const auto as_segments_copy = as_segments(item);
        REQUIRE_EQ(as_segments_copy.size(), 1U);
        CHECK(as_segments_copy[0].is_owned());
        CHECK_EQ(to_hex(flatten_segments(as_segments_copy)), "820102");

        const auto borrowed_segments = borrow_segments(item);
        REQUIRE_EQ(borrowed_segments.size(), 1U);
        CHECK(borrowed_segments[0].is_borrowed());
        CHECK_EQ(borrowed_segments[0].data(), bytes.data());
        CHECK_EQ(to_hex(flatten_segments(borrowed_segments)), "820102");

        const auto encoded_segments = encode_encoded_segments(item);
        REQUIRE_EQ(encoded_segments.size(), 1U);
        CHECK(encoded_segments[0].is_borrowed());
        CHECK_EQ(encoded_segments[0].data(), bytes.data());
        CHECK_EQ(to_hex(flatten_segments(encoded_segments)), "820102");

        const auto copied_segments = copy_segments(item);
        REQUIRE_EQ(copied_segments.size(), 1U);
        CHECK(copied_segments[0].is_owned());
        CHECK_EQ(to_hex(flatten_segments(copied_segments)), "820102");

        const auto encoded_segments_copy = encode_encoded_segments_copy(item);
        REQUIRE_EQ(encoded_segments_copy.size(), 1U);
        CHECK(encoded_segments_copy[0].is_owned());
        CHECK_EQ(to_hex(flatten_segments(encoded_segments_copy)), "820102");
    }

    {
        auto bytes = to_bytes("9f018202039f0405ffff");
        auto dec   = make_decoder(bytes);

        encoded_array_view array;
        REQUIRE(dec(array));

        const auto segments = to_segments(array);
        const auto flat     = flatten_segments(segments);

        REQUIRE_EQ(segments.size(), 1U);
        CHECK(segments[0].is_owned());
        CHECK_EQ(to_hex(flat), "9f018202039f0405ffff");

        const auto borrowed_segments = borrow_segments(array);
        REQUIRE_EQ(borrowed_segments.size(), 1U);
        CHECK(borrowed_segments[0].is_borrowed());
        CHECK_EQ(borrowed_segments[0].data(), bytes.data());
    }

    {
        auto bytes     = to_bytes("a10102");
        auto dec       = make_decoder(bytes);
        using map_view = typename decltype(dec)::raw_encoded_map_view;

        map_view map;
        REQUIRE(dec(map));

        const auto segments = to_segments(map);
        REQUIRE_EQ(segments.size(), 1U);
        CHECK(segments[0].is_owned());
        CHECK_EQ(to_hex(flatten_segments(segments)), "a10102");

        const auto borrowed_segments = borrow_segments(map);
        REQUIRE_EQ(borrowed_segments.size(), 1U);
        CHECK(borrowed_segments[0].is_borrowed());
        CHECK_EQ(borrowed_segments[0].data(), bytes.data());
    }

    {
        auto bytes = to_bytes("f5820102f4");
        auto dec   = make_decoder(bytes);

        bool               prefix{};
        encoded_array_view array;
        bool               suffix{};
        REQUIRE(dec(prefix, array, suffix));

        const auto segments = to_segments(array);
        REQUIRE_EQ(segments.size(), 1U);
        CHECK(segments[0].is_owned());
        CHECK_EQ(to_hex(flatten_segments(segments)), "820102");

        const auto borrowed_segments = borrow_segments(array);
        REQUIRE_EQ(borrowed_segments.size(), 1U);
        CHECK(borrowed_segments[0].is_borrowed());
        CHECK_EQ(borrowed_segments[0].data(), bytes.data() + 1);
    }
}

TEST_CASE("owning span-backed raw encoded views are copied into segments") {
    cbor_segments segments;
    {
        auto view = owning_encoded_item_view{copyable_owning_byte_view{to_bytes("820102")}};

        static_assert(!CanBorrowSegments<owning_encoded_item_view>);
        static_assert(!CanEncodeEncodedSegments<owning_encoded_item_view>);
        static_assert(!CanVisitBorrowedSegments<owning_encoded_item_view>);
        segments = to_segments(view);

        REQUIRE_EQ(segments.size(), 1U);
        CHECK(segments[0].is_owned());
    }

    CHECK_EQ(to_hex(flatten_segments(segments)), "820102");

    segments = to_segments(owning_encoded_item_view{copyable_owning_byte_view{to_bytes("83010203")}});

    REQUIRE_EQ(segments.size(), 1U);
    CHECK(segments[0].is_owned());
    CHECK_EQ(to_hex(flatten_segments(segments)), "83010203");
}

TEST_CASE("non-contiguous raw encoded views use explicit owned segment copy fallback") {
    auto                 contiguous = to_bytes("9f01820203ff");
    std::list<std::byte> bytes(contiguous.begin(), contiguous.end());
    auto                 dec = make_decoder(bytes);
    using array_view         = typename decltype(dec)::raw_encoded_array_view;

    static_assert(CanEncodeSegments<array_view>);
    static_assert(!CanVisitBorrowedSegments<array_view>);
    static_assert(!CanBorrowSegments<array_view>);

    array_view array;
    REQUIRE(dec(array));

    std::vector<std::byte> appended;
    append_encoded_segments(appended, array);
    CHECK_EQ(to_hex(appended), "9f01820203ff");

    const auto segments = to_segments(array);
    const auto flat     = flatten_segments(segments);

    REQUIRE_EQ(segments.size(), 1U);
    CHECK(segments[0].is_owned());
    CHECK_EQ(to_hex(flat), "9f01820203ff");
}
