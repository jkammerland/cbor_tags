#pragma once

#include "fuzztest/fuzztest.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cstdint>
#include <deque>
#include <gtest/gtest.h>
#include <list>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace cbor_fuzz {
using namespace cbor::tags;
using bytes = std::vector<std::uint8_t>;

inline auto text_domain() { return fuzztest::OverlapOf(fuzztest::String().WithMaxSize(128), fuzztest::Utf8String()); }
inline auto blob_domain() { return fuzztest::VectorOf(fuzztest::Arbitrary<std::uint8_t>()).WithMaxSize(256); }
inline auto integers_domain() { return fuzztest::VectorOf(fuzztest::Arbitrary<std::int64_t>()).WithMaxSize(64); }

struct record {
    static constexpr std::uint64_t cbor_tag = 100;
    std::uint64_t                  id{};
    std::string                    label;
    std::vector<std::int64_t>      samples;
    std::optional<std::string>     note;
    bool                           operator==(const record &) const = default;
};

inline auto record_domain() {
    return fuzztest::StructOf<record>(fuzztest::Arbitrary<std::uint64_t>(), text_domain(), integers_domain(),
                                      fuzztest::OptionalOf(text_domain()));
}

template <typename T, typename Buffer> void decode_equal(const T &expected, const Buffer &buffer) {
    T    actual{};
    auto dec = make_decoder(buffer);
    ASSERT_TRUE(dec(actual));

    EXPECT_EQ(detail::unwrap_bounded_size(actual), detail::unwrap_bounded_size(expected));
    EXPECT_EQ(dec.tell(), std::ranges::end(buffer));
}

template <typename T> void roundtrip(const T &value) {
    bytes buffer;
    auto  enc = make_encoder(buffer);
    ASSERT_TRUE(enc(value));
    ASSERT_FALSE(buffer.empty());

    decode_equal(value, buffer);

    const std::deque<std::uint8_t> segmented(buffer.begin(), buffer.end());
    decode_equal(value, segmented);

    const std::list<std::uint8_t> storage(buffer.begin(), buffer.end());
    const auto                    unsized = std::ranges::subrange(storage.begin(), storage.end());
    static_assert(!std::ranges::sized_range<decltype(unsized)>);
    decode_equal(value, unsized);
}
} // namespace cbor_fuzz
