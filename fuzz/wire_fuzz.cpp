#include "wire_domains.h"

#include <cbor_tags/cbor_raw_views.h>
#include <cbor_tags/cbor_traversal.h>

namespace cbor_fuzz {
template <typename T, typename Buffer> void check_decode(const Buffer &buffer, const T &reference, bool reference_ok) {
    T          value{};
    auto       dec    = make_decoder(buffer);
    const auto result = dec(value);

    ASSERT_EQ(result.has_value(), reference_ok);
    if (result) {
        EXPECT_EQ(detail::unwrap_bounded_size(value), detail::unwrap_bounded_size(reference));
    }
    // Failure may leave a prefix, and availability checks differ by range kind.
    // Do not require rollback, identical failure offsets or identical error precedence.
}

template <typename T> void check_ranges(const bytes &input) {
    T          reference{};
    auto       dec    = make_decoder(input);
    const auto result = dec(reference);

    const std::deque<std::uint8_t> segmented(input.begin(), input.end());
    check_decode(segmented, reference, result.has_value());

    const std::list<std::uint8_t> storage(input.begin(), input.end());
    const auto                    unsized = std::ranges::subrange(storage.begin(), storage.end());
    check_decode(unsized, reference, result.has_value());

    if (result) {
        roundtrip(reference);
    }
}

void scalar_decoding(const bytes &input) {
    check_ranges<std::int64_t>(input);
    check_ranges<std::uint64_t>(input);
    check_ranges<bool>(input);
    check_ranges<simple>(input);
    check_ranges<std::optional<std::int64_t>>(input);
}
FUZZ_TEST(CborWire, scalar_decoding).WithDomains(wire_domain()).WithSeeds(wire_seeds);

void container_decoding(const bytes &input) {
    check_ranges<max_size<std::string, 4096>>(input);
    check_ranges<max_size<std::vector<std::byte>, 4096>>(input);
    check_ranges<max_size<std::vector<std::int64_t>, 256>>(input);
    check_ranges<max_size<std::map<std::int64_t, std::int64_t>, 128>>(input);
    check_ranges<std::array<std::int64_t, 3>>(input);
}
FUZZ_TEST(CborWire, container_decoding).WithDomains(wire_domain()).WithSeeds(wire_seeds);

void variant_decoding(const bytes &input) {
    check_ranges<std::variant<std::int64_t, std::string, bool>>(input);
    check_ranges<record>(input);
}
FUZZ_TEST(CborWire, variant_decoding).WithDomains(wire_domain()).WithSeeds(wire_seeds);

void raw_item_view(const bytes &input) {
    encoded_item_view view;
    auto              dec    = make_decoder(input);
    const auto        result = dec(view);
    if (!result) {
        return;
    }

    ASSERT_GT(view.size(), 0U);
    ASSERT_LE(view.size(), input.size());
    EXPECT_EQ(static_cast<std::size_t>(dec.tell() - input.begin()), view.size());
    auto expected = std::as_bytes(std::span(input)).first(view.size());
    EXPECT_TRUE(std::ranges::equal(view.span(), expected));

    bytes encoded;
    auto  enc = make_encoder(encoded);
    ASSERT_TRUE(enc(view));

    EXPECT_TRUE(std::ranges::equal(std::as_bytes(std::span(encoded)), expected));
}
FUZZ_TEST(CborWire, raw_item_view).WithDomains(wire_domain()).WithSeeds(wire_seeds);

void strict_validation(const bytes &input) {
    auto       dec    = make_decoder(input);
    const auto result = validate_item(dec, {.max_depth = 32});

    const std::deque<std::uint8_t> segmented(input.begin(), input.end());
    auto                           other        = make_decoder(segmented);
    const auto                     other_result = validate_item(other, {.max_depth = 32});

    ASSERT_EQ(result.has_value(), other_result.has_value());
    if (result) {
        const auto consumed = dec.tell() - input.begin();
        ASSERT_GT(consumed, 0);
        EXPECT_EQ(consumed, other.tell() - segmented.begin());
    } else {
        EXPECT_EQ(result.error(), other_result.error());
    }
}
FUZZ_TEST(CborWire, strict_validation).WithDomains(wire_domain()).WithSeeds(wire_seeds);

// Literal wire expectations stay in this translation unit, separate from round trips.
TEST(CborWireSeeds, empty_input_is_incomplete) {
    const bytes   input;
    std::uint64_t value{};
    auto          dec    = make_decoder(input);
    auto          result = dec(value);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), status_code::incomplete);
}
TEST(CborWireSeeds, vocabulary_exercises_each_initial_byte) {
    // Each possible first byte must be exercised, including reserved additional info.
    for (unsigned first = 0; first < 256; ++first) {
        scalar_decoding(bytes{static_cast<std::uint8_t>(first)});
    }
}
} // namespace cbor_fuzz
