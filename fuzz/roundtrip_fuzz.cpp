#include "fuzz_support.h"

#include <bit>
#include <cmath>
#include <limits>

namespace cbor_fuzz {
void signed_roundtrip(std::int64_t value) { roundtrip(value); }
FUZZ_TEST(CborRoundtrip, signed_roundtrip);

void unsigned_roundtrip(std::uint64_t value) { roundtrip(value); }
FUZZ_TEST(CborRoundtrip, unsigned_roundtrip);

void negative_roundtrip(std::uint64_t magnitude) { roundtrip(negative{magnitude}); }
FUZZ_TEST(CborRoundtrip, negative_roundtrip);

void half_roundtrip(std::uint16_t bits) { roundtrip(float16_t{bits}); }
FUZZ_TEST(CborRoundtrip, half_roundtrip);

template <typename T> void floating_roundtrip(T value) {
    bytes buffer;
    auto  enc = make_encoder(buffer);
    ASSERT_TRUE(enc(value));
    T    actual{};
    auto dec = make_decoder(buffer);
    ASSERT_TRUE(dec(actual));
    if (std::isnan(value)) {
        EXPECT_TRUE(std::isnan(actual));
    } else {
        EXPECT_EQ(actual, value);
        EXPECT_EQ(std::signbit(actual), std::signbit(value));
    }
    EXPECT_EQ(dec.tell(), buffer.end());
}
void float_roundtrip(float value) { floating_roundtrip(value); }
void double_roundtrip(double value) { floating_roundtrip(value); }
FUZZ_TEST(CborRoundtrip, float_roundtrip);
FUZZ_TEST(CborRoundtrip, double_roundtrip);

void text_roundtrip(const std::string &value) { roundtrip(value); }
FUZZ_TEST(CborRoundtrip, text_roundtrip).WithDomains(text_domain());

void binary_roundtrip(const bytes &value) {
    const auto             span = std::as_bytes(std::span(value));
    std::vector<std::byte> blob(span.begin(), span.end());
    roundtrip(blob);
}
FUZZ_TEST(CborRoundtrip, binary_roundtrip).WithDomains(blob_domain());

void record_roundtrip(const record &value) { roundtrip(value); }
FUZZ_TEST(CborRoundtrip, record_roundtrip).WithDomains(record_domain());

using choice = std::variant<std::int64_t, std::string, bool, record>;
void variant_roundtrip(const choice &value) { roundtrip(value); }
FUZZ_TEST(CborRoundtrip, variant_roundtrip)
    .WithDomains(fuzztest::VariantOf(fuzztest::Arbitrary<std::int64_t>(), text_domain(), fuzztest::Arbitrary<bool>(), record_domain()));

void map_roundtrip(const std::map<std::string, std::vector<std::int64_t>> &value) { roundtrip(value); }
FUZZ_TEST(CborRoundtrip, map_roundtrip).WithDomains(fuzztest::MapOf(text_domain(), integers_domain()).WithMaxSize(16));

void strict_integer_boundaries(std::int64_t value) {
    bytes buffer;
    auto  enc = make_encoder(buffer);
    ASSERT_TRUE(enc(value));
    std::int16_t actual{};
    auto         dec    = make_decoder_with_options<strict_integer_decoder_options>(buffer);
    auto         result = dec(actual);
    const bool   fits   = value >= std::numeric_limits<std::int16_t>::min() && value <= std::numeric_limits<std::int16_t>::max();
    ASSERT_EQ(result.has_value(), fits);
    if (fits) {
        EXPECT_EQ(actual, value);
    } else {
        EXPECT_EQ(result.error(), status_code::no_match_for_int_on_buffer);
    }
}
FUZZ_TEST(CborRoundtrip, strict_integer_boundaries);

void bounded_text(const std::string &value, std::uint8_t limit) {
    bytes buffer;
    auto  enc = make_encoder(buffer);
    ASSERT_TRUE(enc(value));
    std::string decoded;
    auto        bounded = as_bounded_size(decoded, 0, limit);
    auto        dec     = make_decoder(buffer);
    auto        result  = dec(bounded);
    ASSERT_EQ(result.has_value(), value.size() <= limit);
    if (result)
        EXPECT_EQ(decoded, value);
    else
        EXPECT_EQ(result.error(), status_code::size_limit_exceeded);
}
FUZZ_TEST(CborRoundtrip, bounded_text).WithDomains(text_domain(), fuzztest::Arbitrary<std::uint8_t>());

void record_truncation(const record &value, std::size_t cut) {
    bytes buffer;
    auto  enc = make_encoder(buffer);
    ASSERT_TRUE(enc(value));
    ASSERT_FALSE(buffer.empty());
    buffer.resize(cut % buffer.size());
    auto verify = [](const auto &prefix) {
        record decoded;
        auto   dec    = make_decoder(prefix);
        auto   result = dec(decoded);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), status_code::incomplete);
    };
    verify(buffer);
    const std::list<std::uint8_t> storage(buffer.begin(), buffer.end());
    const auto                    unsized = std::ranges::subrange(storage.begin(), storage.end());
    verify(unsized);
}
FUZZ_TEST(CborRoundtrip, record_truncation).WithDomains(record_domain(), fuzztest::Arbitrary<std::size_t>());
} // namespace cbor_fuzz
