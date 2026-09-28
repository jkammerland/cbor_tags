#include "fuzz_support.h"
#include "wire_domains.h"

#include <cbor_tags/cbor_lazy_tags.h>
#include <cbor_tags/cbor_segments.h>
#include <cbor_tags/extensions/rfc8746_typed_arrays.h>
#include <cbor_tags/extensions/smart_ptr.h>

namespace cbor_fuzz {
using namespace cbor::tags::ext::rfc8746;

template <typename T, typed_array_byte_order Order> void typed_roundtrip(const std::vector<T> &input) {
    typed_array<T, Order> value(input);
    bytes                 buffer;
    auto                  enc = make_encoder<typed_array_codec>(buffer);
    ASSERT_TRUE(enc(value));
    auto verify = [&](const auto &data) {
        typed_array<T, Order> decoded;
        auto                  dec = make_decoder<typed_array_codec>(data);
        ASSERT_TRUE(dec(decoded));
        EXPECT_EQ(decoded.values(), input);
        EXPECT_EQ(dec.tell(), data.end());
    };
    verify(buffer);
    const std::deque<std::uint8_t> segmented(buffer.begin(), buffer.end());
    verify(segmented);
    const std::list<std::uint8_t> storage(buffer.begin(), buffer.end());
    const auto                    unsized = std::ranges::subrange(storage.begin(), storage.end());
    verify(unsized);
}

void typed_signed_arrays(const std::vector<std::int64_t> &input) {
    typed_roundtrip<std::int64_t, typed_array_byte_order::little>(input);
    typed_roundtrip<std::int64_t, typed_array_byte_order::big>(input);
}
FUZZ_TEST(CborExtensions, typed_signed_arrays).WithDomains(integers_domain());

void typed_unsigned_arrays(const std::vector<std::uint32_t> &input) {
    typed_roundtrip<std::uint32_t, typed_array_byte_order::little>(input);
    typed_roundtrip<std::uint32_t, typed_array_byte_order::big>(input);
}
FUZZ_TEST(CborExtensions, typed_unsigned_arrays).WithDomains(fuzztest::VectorOf(fuzztest::Arbitrary<std::uint32_t>()).WithMaxSize(64));

void typed_float_arrays(const std::vector<double> &input) {
    typed_roundtrip<double, typed_array_byte_order::little>(input);
    typed_roundtrip<double, typed_array_byte_order::big>(input);
}
FUZZ_TEST(CborExtensions, typed_float_arrays).WithDomains(fuzztest::VectorOf(fuzztest::Finite<double>()).WithMaxSize(64));

void typed_array_wire(const bytes &input) {
    typed_array<std::int32_t>      contiguous;
    auto                           dec    = make_decoder<typed_array_codec>(input);
    const auto                     result = dec(contiguous);
    const std::deque<std::uint8_t> segmented(input.begin(), input.end());
    typed_array<std::int32_t>      other;
    auto                           other_dec    = make_decoder<typed_array_codec>(segmented);
    const auto                     other_result = other_dec(other);
    ASSERT_EQ(result.has_value(), other_result.has_value());
    if (result)
        EXPECT_EQ(contiguous.values(), other.values());
    else
        EXPECT_EQ(result.error(), other_result.error());
}
FUZZ_TEST(CborExtensionWire, typed_array_wire).WithDomains(wire_domain()).WithSeeds(wire_seeds);

void lazy_tag_payload(const record &input) {
    bytes buffer;
    auto  enc = make_encoder(buffer);
    ASSERT_TRUE(enc(input));
    auto matches = find_tags<100>(buffer);
    auto it      = matches.begin();
    ASSERT_NE(it, matches.end());
    EXPECT_EQ(it->tag(), 100U);
    // The record's payload is its array of fields; the tag itself has been consumed.
    record actual;
    auto   payload_dec = it->make_decoder();
    ASSERT_TRUE(payload_dec(as_array{4}, actual.id, actual.label, actual.samples, actual.note));
    EXPECT_EQ(actual, input);
    ++it;
    EXPECT_EQ(it, matches.end());
    EXPECT_EQ(matches.status(), status_code::success);
}
FUZZ_TEST(CborExtensions, lazy_tag_payload).WithDomains(record_domain());

void smart_pointer_roundtrip(const std::optional<std::int64_t> &input) {
    using namespace cbor::tags::ext::smart_ptr;
    std::unique_ptr<std::int64_t> source;
    if (input)
        source = std::make_unique<std::int64_t>(*input);
    bytes buffer;
    auto  enc = make_encoder<unique_ptr_codec>(buffer);
    ASSERT_TRUE(enc(source));
    std::unique_ptr<std::int64_t> decoded;
    auto                          dec = make_decoder<unique_ptr_codec>(buffer);
    ASSERT_TRUE(dec(decoded));
    ASSERT_EQ(static_cast<bool>(decoded), input.has_value());
    if (input)
        EXPECT_EQ(*decoded, *input);
    EXPECT_EQ(dec.tell(), buffer.end());
}
FUZZ_TEST(CborExtensions, smart_pointer_roundtrip).WithDomains(fuzztest::OptionalOf(fuzztest::Arbitrary<std::int64_t>()));

void segmented_record(const record &input) {
    const auto segments = encode_item_segments(input);
    ASSERT_TRUE(segments);
    const auto flat = segments->flatten();
    EXPECT_EQ(flat.size(), segments->total_size());
    decode_equal(input, flat);
}
FUZZ_TEST(CborExtensions, segmented_record).WithDomains(record_domain());

void owned_and_borrowed_segments(const bytes &input, std::size_t split) {
    const auto    payload = std::as_bytes(std::span(input));
    const auto    cut     = split % (payload.size() + 1);
    byte_segments segments;
    segments.append_owned(payload.first(cut));
    segments.append_borrowed(payload.subspan(cut));
    auto copied = segments;
    auto moved  = std::move(copied);
    EXPECT_EQ(moved.total_size(), input.size());
    const auto flat = moved.flatten();
    EXPECT_TRUE(std::ranges::equal(flat, payload));
    // Appending after a borrowed segment must leave that borrowed storage intact.
    moved.append_owned(payload);
    const auto twice = moved.flatten();
    ASSERT_EQ(twice.size(), input.size() * 2);
    EXPECT_TRUE(std::ranges::equal(std::span(twice).first(input.size()), payload));
    EXPECT_TRUE(std::ranges::equal(std::span(twice).subspan(input.size()), payload));
}
FUZZ_TEST(CborSegmentsWire, owned_and_borrowed_segments).WithDomains(blob_domain(), fuzztest::Arbitrary<std::size_t>());

void lazy_tag_wire(const bytes &input) {
    auto        matches = find_tags(input, [](std::uint64_t) { return true; });
    std::size_t count   = 0;
    for (const auto &match : matches) {
        ASSERT_LE(++count, input.size());
        ASSERT_GT(match.payload_span().size(), 0U);
        EXPECT_LE(match.payload_span().size(), input.size());
    }
    // Iteration is terminal; read status even if there were no matches.
    EXPECT_NE(status_message(matches.status()), "Unknown CBOR status code");
}
FUZZ_TEST(CborExtensionWire, lazy_tag_wire).WithDomains(wire_domain()).WithSeeds(wire_seeds);
} // namespace cbor_fuzz
