#include "test_util.h"

#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_detail.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_lazy_tags.h>
#include <cbor_tags/cbor_ranges.h>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <iterator>
#include <map>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

using namespace cbor::tags;

namespace {

struct range_regression_not_cbor {};

template <typename R>
concept CanMakeArrayRange = requires(R &&range) { cbor::tags::as_array_range(std::forward<R>(range)); };

template <typename R>
concept CanMakeMapRange = requires(R &&range) { cbor::tags::as_map_range(std::forward<R>(range)); };

using regression_int_array_wrapper = cbor::tags::array_range<std::ranges::ref_view<std::vector<int>>>;
using regression_nested_array      = std::array<regression_int_array_wrapper, 1>;
using regression_bad_array_wrapper = cbor::tags::array_range<std::ranges::single_view<range_regression_not_cbor>>;
using regression_bad_map_entries   = std::array<std::pair<int, regression_bad_array_wrapper>, 1>;

static_assert(CanMakeArrayRange<regression_nested_array &>);
static_assert(!CanMakeMapRange<regression_bad_map_entries &>);
static_assert(cbor::tags::IsFixedArray<std::span<int>>);
static_assert(cbor::tags::IsFixedArray<std::span<int, 2>>);
static_assert(!cbor::tags::IsFixedArray<std::span<const std::byte, 2>>);
static_assert(!cbor::tags::IsFixedArray<std::span<const std::byte>>);
static_assert(cbor::tags::detail::is_static_extent_span_v<std::span<const std::byte, 2>>);
static_assert(CanMakeArrayRange<std::vector<bool> &>);

struct counting_sized_bidirectional_bytes {
    using size_type = std::size_t;

    struct iterator {
        using iterator_category = std::bidirectional_iterator_tag;
        using iterator_concept  = std::bidirectional_iterator_tag;
        using difference_type   = std::ptrdiff_t;
        using value_type        = std::byte;

        const counting_sized_bidirectional_bytes *owner{};
        size_type                                 index{};

        std::byte operator*() const { return owner->bytes[index]; }

        iterator &operator++() {
            ++owner->increments;
            ++index;
            return *this;
        }

        iterator operator++(int) {
            auto copy = *this;
            ++(*this);
            return copy;
        }

        iterator &operator--() {
            ++owner->increments;
            --index;
            return *this;
        }

        iterator operator--(int) {
            auto copy = *this;
            --(*this);
            return copy;
        }

        friend bool operator==(const iterator &, const iterator &) = default;
    };

    std::vector<std::byte> bytes;
    mutable size_type      increments{};

    explicit counting_sized_bidirectional_bytes(size_type size) : bytes(size, std::byte{0x01}) {}

    iterator  begin() const { return iterator{this, 0}; }
    iterator  end() const { return iterator{this, bytes.size()}; }
    size_type size() const noexcept { return bytes.size(); }
};

static_assert(cbor::tags::CborInputBuffer<counting_sized_bidirectional_bytes>);

struct range_regression_move_only_value {
    int value{};

    range_regression_move_only_value()                                                        = default;
    range_regression_move_only_value(const range_regression_move_only_value &)                = delete;
    range_regression_move_only_value &operator=(const range_regression_move_only_value &)     = delete;
    range_regression_move_only_value(range_regression_move_only_value &&) noexcept            = default;
    range_regression_move_only_value &operator=(range_regression_move_only_value &&) noexcept = default;

    template <typename Decoder> constexpr auto decode(Decoder &dec) { return dec(value); }
};

struct range_regression_non_move_assignable_value {
    int value{};

    range_regression_non_move_assignable_value()                                                              = default;
    range_regression_non_move_assignable_value(const range_regression_non_move_assignable_value &)            = delete;
    range_regression_non_move_assignable_value &operator=(const range_regression_non_move_assignable_value &) = delete;
    range_regression_non_move_assignable_value(range_regression_non_move_assignable_value &&) noexcept        = default;
    range_regression_non_move_assignable_value &operator=(range_regression_non_move_assignable_value &&)      = delete;

    template <typename Decoder> constexpr auto decode(Decoder &dec) { return dec(value); }
};

std::vector<std::byte> make_deep_tag_with_array_payload(std::size_t depth) {
    std::vector<std::byte> buffer;
    buffer.reserve(depth + 5);
    for (std::size_t index = 0; index < depth; ++index) {
        buffer.push_back(std::byte{0x81});
    }
    buffer.push_back(std::byte{0xD8});
    buffer.push_back(std::byte{0x64});
    buffer.push_back(std::byte{0x81});
    buffer.push_back(std::byte{0x18});
    buffer.push_back(std::byte{0x2A});
    return buffer;
}

} // namespace

TEST_CASE("explicit array range wrappers can be nested") {
    std::vector<int> values{1, 2};
    auto             nested = std::array{as_array_range(values)};

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);

    REQUIRE(enc(as_array_range(nested)));
    CHECK_EQ(to_hex(buffer), "81820102");
}

TEST_CASE("explicit array range wrappers encode proxy reference ranges") {
    std::vector<bool>      flags{true, false, true};
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);

    REQUIRE(enc(as_array_range(flags)));
    CHECK_EQ(to_hex(buffer), "83f5f4f5");
}

TEST_CASE("explicit map range wrappers keep proxy pair components strict") {
    std::vector<bool> flags{true, false};
    auto              proxy_pairs = flags | std::views::transform([](auto bit) { return std::pair{1, bit}; });

    CHECK_FALSE(CanMakeMapRange<decltype(proxy_pairs) &>);
}

TEST_CASE("explicit const nested wrappers reject non-const-iterable views early") {
    std::vector<int> values{1, 2};

    auto mutable_filtered = values | std::views::filter([](int) { return true; });
    auto mutable_nested   = std::array{as_array_range(mutable_filtered)};
    static_assert(CanMakeArrayRange<decltype(mutable_nested) &>);

    auto       filtered = values | std::views::filter([](int) { return true; });
    const auto nested   = std::array{as_array_range(filtered)};
    static_assert(!CanMakeArrayRange<decltype(nested) &>);
}

TEST_CASE("byte string range wrappers reject zero chunk size for sized and unsized ranges") {
    {
        std::array<std::byte, 2> bytes{std::byte{0x01}, std::byte{0x02}};
        std::vector<std::byte>   buffer;
        auto                     enc = make_encoder(buffer);

        auto result = enc(as_bstr_range(bytes, 0));

        CHECK_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK(buffer.empty());
    }

    {
        std::array<std::byte, 2> bytes{std::byte{0x01}, std::byte{0x02}};
        auto                     unsized = bytes | std::views::filter([](std::byte) { return true; });

        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);

        auto result = enc(as_bstr_range(unsized, 0));

        CHECK_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK(buffer.empty());
    }
}

TEST_CASE("sized non-contiguous readers use size for offset bounds checks") {
    counting_sized_bidirectional_bytes                                    input{1024};
    cbor::tags::detail::reader<counting_sized_bidirectional_bytes, false> reader{input};

    CHECK_FALSE(reader.empty(input, 1023));
    CHECK(reader.empty(input, 1024));
    CHECK_EQ(input.increments, 0);

    CHECK_EQ(reader.read(input), std::byte{0x01});
    CHECK_EQ(input.increments, 1);
    CHECK_FALSE(reader.empty(input, 1022));
    CHECK(reader.empty(input, 1023));
    CHECK_EQ(input.increments, 1);
}

TEST_CASE("lazy tag scanner applies remaining depth budget to matched payload validation") {
    {
        auto buffer = make_deep_tag_with_array_payload(254);
        auto view   = find_tags<100>(buffer);
        auto it     = view.begin();

        REQUIRE(it != view.end());
        CHECK_EQ(it->tag(), 100);
        CHECK_EQ(to_hex(it->payload_range()), "81182a");

        ++it;
        CHECK(it == view.end());
        CHECK_EQ(view.status(), status_code::success);
    }

    {
        auto buffer = make_deep_tag_with_array_payload(255);
        auto view   = find_tags<100>(buffer);
        auto it     = view.begin();

        CHECK(it == view.end());
        CHECK(view.failed());
        CHECK_EQ(view.status(), status_code::error);
    }
}

TEST_CASE("static extent spans decode with fixed-size length checks") {
    {
        auto buffer = to_bytes("820102");
        auto dec    = make_decoder(buffer);

        std::array<int, 2> storage{};
        std::span<int, 2>  decoded{storage};

        auto result = dec(decoded);

        REQUIRE(result);
        CHECK_EQ(storage, (std::array{1, 2}));
    }

    {
        auto buffer = to_bytes("83010203");
        auto dec    = make_decoder(buffer);

        std::array<int, 2> storage{-1, -1};
        std::span<int, 2>  decoded{storage};

        auto result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::unexpected_group_size);
        CHECK_EQ(storage, (std::array{-1, -1}));
    }
}

TEST_CASE("fixed-size byte string targets validate length before reading payload") {
    {
        std::vector<std::byte> buffer{std::byte{0x42}, std::byte{0x01}};
        auto                   dec = make_decoder(buffer);

        std::array<std::byte, 1> decoded{std::byte{0xCC}};
        auto                     result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::unexpected_group_size);
        CHECK_EQ(decoded[0], std::byte{0xCC});
    }

    {
        std::vector<std::byte> buffer{std::byte{0x42}, std::byte{0x01}};
        auto                   dec = make_decoder(buffer);

        std::array<std::byte, 2> decoded{std::byte{0xCC}, std::byte{0xCC}};
        auto                     result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::incomplete);
        CHECK_EQ(decoded, (std::array{std::byte{0xCC}, std::byte{0xCC}}));
    }
}

TEST_CASE("const static extent byte spans validate length before rebinding") {
    {
        auto buffer = to_bytes("420102");
        auto dec    = make_decoder(buffer);

        std::array<std::byte, 2>      storage{std::byte{0xAA}, std::byte{0xBB}};
        std::span<const std::byte, 2> decoded{storage};

        auto result = dec(decoded);

        REQUIRE(result);
        CHECK_EQ(decoded.data(), buffer.data() + 1);
        CHECK_EQ(to_hex(decoded), "0102");
    }

    {
        auto buffer = to_bytes("4101");
        auto dec    = make_decoder(buffer);

        std::array<std::byte, 2>      storage{std::byte{0xAA}, std::byte{0xBB}};
        std::span<const std::byte, 2> decoded{storage};

        auto result = dec(decoded);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::unexpected_group_size);
        CHECK_EQ(decoded.data(), storage.data());
        CHECK_EQ(to_hex(decoded), "aabb");
    }
}

TEST_CASE("map decode moves decoded entries into associative containers") {
    {
        auto bytes = to_bytes("a201020103");
        auto dec   = make_decoder(bytes);

        std::map<int, range_regression_move_only_value> decoded;
        auto                                            result = dec(decoded);

        REQUIRE(result);
        REQUIRE_EQ(decoded.size(), 1);
        CHECK_EQ(decoded.at(1).value, 3);
    }

    {
        auto bytes = to_bytes("a201020103");
        auto dec   = make_decoder(bytes);

        std::multimap<int, range_regression_move_only_value> decoded;
        auto                                                 result = dec(decoded);

        REQUIRE(result);
        CHECK_EQ(decoded.count(1), 2);
    }

    {
        auto bytes = to_bytes("bf0204ff");
        auto dec   = make_decoder(bytes);

        std::map<int, range_regression_move_only_value> decoded;
        auto                                            result = dec(decoded);

        REQUIRE(result);
        REQUIRE_EQ(decoded.size(), 1);
        CHECK_EQ(decoded.at(2).value, 4);
    }
}

TEST_CASE("map decode inserts move-constructible non-move-assignable mapped values") {
    {
        auto bytes = to_bytes("a10102");
        auto dec   = make_decoder(bytes);

        std::map<int, range_regression_non_move_assignable_value> decoded;
        auto                                                      result = dec(decoded);

        REQUIRE(result);
        REQUIRE_EQ(decoded.size(), 1);
        CHECK_EQ(decoded.at(1).value, 2);
    }

    {
        auto bytes = to_bytes("a201020103");
        auto dec   = make_decoder(bytes);

        std::map<int, range_regression_non_move_assignable_value> decoded;
        auto                                                      result = dec(decoded);

        REQUIRE(result);
        REQUIRE_EQ(decoded.size(), 1);
        CHECK_EQ(decoded.at(1).value, 2);
    }
}
