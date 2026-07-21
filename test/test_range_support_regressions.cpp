#include "test_util.h"

#include <algorithm>
#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_detail.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_lazy_tags.h>
#include <cbor_tags/cbor_ranges.h>
#include <cbor_tags/cbor_raw_views.h>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <initializer_list>
#include <iterator>
#include <list>
#include <map>
#include <memory_resource>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace cbor::tags;

namespace {

struct range_regression_not_cbor {};

template <typename R>
concept CanMakeArrayRange = requires(R &&range) { as_array_range(std::forward<R>(range)); };

template <typename R>
concept CanMakeMapRange = requires(R &&range) { as_map_range(std::forward<R>(range)); };

template <typename R>
concept CanMakeTstrRange = requires(R &&range) { as_tstr_range(std::forward<R>(range)); };

using regression_int_array_wrapper = array_range<std::ranges::ref_view<std::vector<int>>>;
using regression_nested_array      = std::array<regression_int_array_wrapper, 1>;
using regression_bad_array_wrapper = array_range<std::ranges::single_view<range_regression_not_cbor>>;
using regression_bad_map_entries   = std::array<std::pair<int, regression_bad_array_wrapper>, 1>;

static_assert(CanMakeArrayRange<regression_nested_array &>);
static_assert(!CanMakeMapRange<regression_bad_map_entries &>);
static_assert(IsFixedArray<std::span<int>>);
static_assert(IsFixedArray<std::span<int, 2>>);
static_assert(!IsFixedArray<std::span<const std::byte, 2>>);
static_assert(!IsFixedArray<std::span<const std::byte>>);
static_assert(detail::is_static_extent_span_v<std::span<const std::byte, 2>>);
static_assert(CanMakeArrayRange<std::vector<bool> &>);
static_assert(CanMakeTstrRange<std::string &>);
static_assert(CanMakeTstrRange<std::array<char, 2> &>);
static_assert(!CanMakeTstrRange<std::array<std::byte, 2> &>);
static_assert(!CanMakeTstrRange<std::array<unsigned char, 2> &>);

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

static_assert(CborInputBuffer<counting_sized_bidirectional_bytes>);

struct byte_view_with_unrelated_span : std::ranges::view_interface<byte_view_with_unrelated_span> {
    std::array<std::uint8_t, 3> bytes{};

    byte_view_with_unrelated_span() = default;
    explicit byte_view_with_unrelated_span(std::array<std::uint8_t, 3> input) : bytes(input) {}

    auto begin() noexcept { return bytes.begin(); }
    auto begin() const noexcept { return bytes.begin(); }
    auto end() noexcept { return bytes.end(); }
    auto end() const noexcept { return bytes.end(); }
    auto data() const noexcept { return bytes.data(); }
    auto size() const noexcept { return bytes.size(); }

    std::span<const std::uint8_t> span() const noexcept { return bytes; }
};

static_assert(std::ranges::view<byte_view_with_unrelated_span>);
static_assert(IsByteLikeRange<byte_view_with_unrelated_span>);

struct counting_append_buffer {
    using value_type     = std::byte;
    using size_type      = std::size_t;
    using storage_type   = std::vector<value_type>;
    using iterator       = storage_type::iterator;
    using const_iterator = storage_type::const_iterator;

    storage_type bytes;
    size_type    push_back_calls{};
    size_type    range_insert_calls{};
    size_type    range_inserted_bytes{};

    void push_back(value_type byte) {
        ++push_back_calls;
        bytes.push_back(byte);
    }

    iterator       begin() noexcept { return bytes.begin(); }
    iterator       end() noexcept { return bytes.end(); }
    const_iterator begin() const noexcept { return bytes.begin(); }
    const_iterator end() const noexcept { return bytes.end(); }
    size_type      size() const noexcept { return bytes.size(); }

    template <typename Iterator> iterator insert(const_iterator position, Iterator first, Iterator last) {
        const auto before = bytes.size();
        auto       result = bytes.insert(position, first, last);
        ++range_insert_calls;
        range_inserted_bytes += bytes.size() - before;
        return result;
    }

    iterator insert(const_iterator position, std::initializer_list<value_type> values) {
        const auto before = bytes.size();
        auto       result = bytes.insert(position, values);
        ++range_insert_calls;
        range_inserted_bytes += bytes.size() - before;
        return result;
    }
};

static_assert(CborOutputBuffer<counting_append_buffer>);

struct counting_memory_resource : std::pmr::memory_resource {
    std::size_t                allocations{};
    std::pmr::memory_resource *upstream{std::pmr::new_delete_resource()};

  private:
    void *do_allocate(std::size_t bytes, std::size_t alignment) override {
        ++allocations;
        return upstream->allocate(bytes, alignment);
    }

    void do_deallocate(void *ptr, std::size_t bytes, std::size_t alignment) override { upstream->deallocate(ptr, bytes, alignment); }

    bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override { return this == &other; }
};

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

TEST_CASE("text string range wrappers reject zero chunk size for sized and unsized ranges") {
    {
        std::string            text{"hi"};
        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);

        auto result = enc(as_tstr_range(text, 0));

        CHECK_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK(buffer.empty());
    }

    {
        std::string text{"hi"};
        auto        unsized = text | std::views::filter([](char) { return true; });

        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);

        auto result = enc(as_tstr_range(unsized, 0));

        CHECK_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK(buffer.empty());
    }
}

TEST_CASE("contiguous sized byte string ranges match direct byte string encoding") {
    const std::vector<std::byte> bytes{std::byte{0x00}, std::byte{0x01}, std::byte{0x7F}, std::byte{0x80}, std::byte{0xFF}};

    std::vector<std::byte> direct;
    {
        auto enc = make_encoder(direct);
        REQUIRE(enc(bytes));
    }

    {
        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);
        REQUIRE(enc(as_bstr_range(bytes)));
        CHECK_EQ(buffer, direct);
    }

    {
        const std::array<std::byte, 5> array_bytes{std::byte{0x00}, std::byte{0x01}, std::byte{0x7F}, std::byte{0x80}, std::byte{0xFF}};
        std::vector<std::byte>         buffer;
        auto                           enc = make_encoder(buffer);
        REQUIRE(enc(as_bstr_range(array_bytes)));
        CHECK_EQ(buffer, direct);
    }

    {
        const std::span<const std::byte> span_bytes{bytes};
        std::vector<std::byte>           buffer;
        auto                             enc = make_encoder(buffer);
        REQUIRE(enc(as_bstr_range(span_bytes)));
        CHECK_EQ(buffer, direct);
    }

    {
        const std::vector<std::uint8_t> uint8_bytes{0x00, 0x01, 0x7F, 0x80, 0xFF};
        std::vector<std::byte>          buffer;
        auto                            enc = make_encoder(buffer);
        REQUIRE(enc(as_bstr_range(uint8_bytes)));
        CHECK_EQ(buffer, direct);
    }
}

TEST_CASE("contiguous sized text string ranges match direct text string encoding") {
    const std::string text{"hello"};

    std::vector<std::byte> direct;
    {
        auto enc = make_encoder(direct);
        REQUIRE(enc(text));
    }

    {
        std::vector<std::byte> buffer;
        auto                   enc = make_encoder(buffer);
        REQUIRE(enc(as_tstr_range(text)));
        CHECK_EQ(buffer, direct);
    }

    {
        const std::array<char, 5> chars{'h', 'e', 'l', 'l', 'o'};
        std::vector<std::byte>    buffer;
        auto                      enc = make_encoder(buffer);
        REQUIRE(enc(as_tstr_range(chars)));
        CHECK_EQ(buffer, direct);
    }

    {
        std::vector<std::byte> buffer;
        auto                   enc   = make_encoder(buffer);
        auto                   upper = text | std::views::transform(
                                                  [](char value) { return static_cast<char>(value >= 'a' && value <= 'z' ? value - 'a' + 'A' : value); });

        REQUIRE(enc(as_tstr_range(upper)));
        CHECK_EQ(to_hex(buffer), "6548454c4c4f");
    }
}

TEST_CASE("byte string range wrappers encode to exact-size fixed output buffers") {
    const std::array<std::byte, 5> bytes{std::byte{0x00}, std::byte{0x01}, std::byte{0x7F}, std::byte{0x80}, std::byte{0xFF}};

    {
        std::array<std::byte, 6> output{};
        auto                     enc = make_encoder(output);

        REQUIRE(enc(as_bstr_range(bytes)));
        CHECK_EQ(to_hex(output), "4500017f80ff");
    }

    {
        std::array<std::byte, 6> storage{};
        std::span<std::byte, 6>  output{storage};
        auto                     enc = make_encoder(output);

        REQUIRE(enc(as_bstr_range(bytes)));
        CHECK_EQ(to_hex(storage), "4500017f80ff");
    }
}

TEST_CASE("text string range wrappers encode to exact-size fixed output buffers") {
    const std::string text{"hello"};

    {
        std::array<std::byte, 6> output{};
        auto                     enc = make_encoder(output);

        REQUIRE(enc(as_tstr_range(text)));
        CHECK_EQ(to_hex(output), "6568656c6c6f");
    }

    {
        std::array<std::byte, 6> storage{};
        std::span<std::byte, 6>  output{storage};
        auto                     enc = make_encoder(output);

        REQUIRE(enc(as_tstr_range(text)));
        CHECK_EQ(to_hex(storage), "6568656c6c6f");
    }
}

TEST_CASE("byte string range wrappers report too-small fixed output buffers") {
    const std::array<std::byte, 5> bytes{std::byte{0x00}, std::byte{0x01}, std::byte{0x7F}, std::byte{0x80}, std::byte{0xFF}};
    std::array<std::byte, 5>       output{std::byte{0xCC}, std::byte{0xCC}, std::byte{0xCC}, std::byte{0xCC}, std::byte{0xCC}};
    auto                           enc = make_encoder(output);

    const auto result = enc(as_bstr_range(bytes));

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::error);
    CHECK_EQ(to_hex(output), "45cccccccc");
}

TEST_CASE("text string range wrappers report too-small fixed output buffers") {
    const std::string        text{"hello"};
    std::array<std::byte, 5> output{std::byte{0xCC}, std::byte{0xCC}, std::byte{0xCC}, std::byte{0xCC}, std::byte{0xCC}};
    auto                     enc = make_encoder(output);

    const auto result = enc(as_tstr_range(text));

    REQUIRE_FALSE(result);
    CHECK_EQ(result.error(), status_code::error);
    CHECK_EQ(to_hex(output), "65cccccccc");
}

TEST_CASE("raw encoded views encode to fixed output buffers without normalization") {
    auto bytes = to_bytes("820102");
    auto dec   = make_decoder(bytes);

    encoded_array_view array;
    REQUIRE(dec(array));

    {
        std::array<std::byte, 3> output{};
        auto                     enc = make_encoder(output);

        REQUIRE(enc(array));
        CHECK_EQ(to_hex(output), "820102");
    }

    {
        std::array<std::byte, 3> storage{};
        std::span<std::byte, 3>  output{storage};
        auto                     enc = make_encoder(output);

        REQUIRE(enc(array));
        CHECK_EQ(to_hex(storage), "820102");
    }

    {
        std::array<std::byte, 2> output{std::byte{0xCC}, std::byte{0xCC}};
        auto                     enc = make_encoder(output);

        const auto result = enc(array);

        REQUIRE_FALSE(result);
        CHECK_EQ(result.error(), status_code::error);
        CHECK_EQ(to_hex(output), "cccc");
    }
}

TEST_CASE("contiguous sized byte string ranges use bulk append when available") {
    const std::vector<std::uint8_t> bytes{0x00, 0x01, 0x7F, 0x80, 0xFF};
    counting_append_buffer          buffer;
    auto                            enc = make_encoder(buffer);

    REQUIRE(enc(as_bstr_range(bytes)));

    CHECK_EQ(to_hex(buffer), "4500017f80ff");
    CHECK_EQ(buffer.push_back_calls, 1);
    CHECK_EQ(buffer.range_insert_calls, 1);
    CHECK_EQ(buffer.range_inserted_bytes, bytes.size());
}

TEST_CASE("non-contiguous std::byte ranges use direct insert when available") {
    const std::list<std::byte> bytes{std::byte{0x00}, std::byte{0xFF}, std::byte{0x10}, std::byte{0x20}};
    counting_append_buffer     buffer;
    auto                       enc = make_encoder(buffer);

    REQUIRE(enc(as_bstr_range(bytes)));

    CHECK_EQ(to_hex(buffer), "4400ff1020");
    CHECK_EQ(buffer.push_back_calls, 1);
    CHECK_EQ(buffer.range_insert_calls, 1);
    CHECK_EQ(buffer.range_inserted_bytes, bytes.size());
}

TEST_CASE("byte string range append ignores unrelated span members") {
    byte_view_with_unrelated_span bytes{{0x01, 0x02, 0x03}};
    std::vector<std::byte>        buffer;
    auto                          enc = make_encoder(buffer);

    REQUIRE(enc(as_bstr_range(std::move(bytes))));

    CHECK_EQ(to_hex(buffer), "43010203");
}

TEST_CASE("non-contiguous sized byte string ranges preserve bytes") {
    const std::list<std::uint8_t> bytes{0x00, 0xFF, 0x10, 0x20};
    std::vector<std::byte>        buffer;
    auto                          enc = make_encoder(buffer);

    REQUIRE(enc(as_bstr_range(bytes)));

    CHECK_EQ(to_hex(buffer), "4400ff1020");
}

TEST_CASE("indefinite byte string range chunks preserve exact headers and payloads") {
    const std::vector<std::byte> bytes{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04}, std::byte{0x05}};
    auto                         unsized = bytes | std::views::filter([](std::byte) { return true; });

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);

    REQUIRE(enc(as_bstr_range(unsized, 3)));

    CHECK_EQ(to_hex(buffer), "5f43010203420405ff");
}

TEST_CASE("indefinite text string range chunks preserve exact headers and payloads") {
    const std::string text{"hello"};
    auto              unsized = text | std::views::filter([](char) { return true; });

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);

    REQUIRE(enc(as_tstr_range(unsized, 2)));

    CHECK_EQ(to_hex(buffer), "7f626865626c6c616fff");
}

TEST_CASE("reserved definite byte string range encode does not allocate") {
    counting_memory_resource          resource;
    std::pmr::vector<std::byte>       buffer{&resource};
    const std::array<std::uint8_t, 4> bytes{0x01, 0x02, 0x03, 0x04};

    buffer.reserve(bytes.size() + 1);
    resource.allocations = 0;

    auto enc = make_encoder(buffer);
    REQUIRE(enc(as_bstr_range(bytes)));

    CHECK_EQ(to_hex(buffer), "4401020304");
    CHECK_EQ(resource.allocations, 0);
}

TEST_CASE("sized non-contiguous readers use size for offset bounds checks") {
    counting_sized_bidirectional_bytes                        input{1024};
    detail::reader<counting_sized_bidirectional_bytes, false> reader{input};

    CHECK_FALSE(reader.empty(input, 1023));
    CHECK(reader.empty(input, 1024));
    CHECK_EQ(input.increments, 0);

    CHECK_EQ(reader.read(input), std::byte{0x01});
    CHECK_EQ(input.increments, 1);
    CHECK_FALSE(reader.empty(input, 1022));
    CHECK(reader.empty(input, 1023));
    CHECK_EQ(input.increments, 1);
}

TEST_CASE("typed decode reads non-contiguous input once without a structural preflight") {
    counting_sized_bidirectional_bytes input{3};
    input.bytes = {std::byte{0x81}, std::byte{0x81}, std::byte{0x00}};

    auto                          dec = make_decoder(input);
    std::vector<std::vector<int>> value;
    auto                          result = dec(value);

    REQUIRE(result);
    REQUIRE_EQ(value.size(), 1U);
    REQUIRE_EQ(value[0].size(), 1U);
    CHECK_EQ(value[0][0], 0);
    CHECK_EQ(input.increments, input.bytes.size());
}

TEST_CASE("bounded indefinite decoding traverses non-contiguous input once") {
    counting_sized_bidirectional_bytes static_input{4};
    static_input.bytes = to_bytes("9f0000ff");
    auto dynamic_input = static_input;

    std::vector<std::uint64_t> static_values;
    auto                       static_dec = make_decoder(static_input);
    REQUIRE(static_dec(as_bounded_size<0, 2>(static_values)));

    std::vector<std::uint64_t> dynamic_values;
    auto                       dynamic_dec = make_decoder(dynamic_input);
    REQUIRE(dynamic_dec(as_bounded_size(dynamic_values, 0, 2)));

    CHECK_EQ(dynamic_values, static_values);
    CHECK_EQ(static_input.increments, static_input.bytes.size());
    CHECK_EQ(dynamic_input.increments, static_input.increments);
}

TEST_CASE("bounded definite decoding adds no input traversal") {
    SUBCASE("array") {
        counting_sized_bidirectional_bytes bounded_input{26};
        bounded_input.bytes[0] = std::byte{0x98};
        bounded_input.bytes[1] = std::byte{0x18};
        std::ranges::fill(bounded_input.bytes.begin() + 2, bounded_input.bytes.end(), std::byte{0x00});
        auto direct_input  = bounded_input;
        auto dynamic_input = bounded_input;

        std::vector<std::uint64_t> direct_values;
        auto                       direct_dec = make_decoder(direct_input);
        REQUIRE(direct_dec(direct_values));

        std::vector<std::uint64_t> bounded_values;
        auto                       bounded_dec = make_decoder(bounded_input);
        REQUIRE(bounded_dec(as_bounded_size<24, 24>(bounded_values)));

        std::vector<std::uint64_t> dynamic_values;
        auto                       dynamic_dec = make_decoder(dynamic_input);
        REQUIRE(dynamic_dec(as_bounded_size(dynamic_values, 24, 24)));

        CHECK_EQ(bounded_values, direct_values);
        CHECK_EQ(dynamic_values, direct_values);
        CHECK_EQ(bounded_input.increments, direct_input.increments);
        CHECK_EQ(dynamic_input.increments, direct_input.increments);
        CHECK_EQ(bounded_input.increments, bounded_input.bytes.size());
    }

    SUBCASE("byte string") {
        counting_sized_bidirectional_bytes bounded_input{26};
        bounded_input.bytes[0] = std::byte{0x58};
        bounded_input.bytes[1] = std::byte{0x18};
        std::ranges::fill(bounded_input.bytes.begin() + 2, bounded_input.bytes.end(), std::byte{0x2a});
        auto direct_input  = bounded_input;
        auto dynamic_input = bounded_input;

        std::vector<std::byte> direct_value;
        auto                   direct_dec = make_decoder(direct_input);
        REQUIRE(direct_dec(direct_value));

        std::vector<std::byte> bounded_value;
        auto                   bounded_dec = make_decoder(bounded_input);
        REQUIRE(bounded_dec(as_bounded_size<24, 24>(bounded_value)));

        std::vector<std::byte> dynamic_value;
        auto                   dynamic_dec = make_decoder(dynamic_input);
        REQUIRE(dynamic_dec(as_bounded_size(dynamic_value, 24, 24)));

        CHECK_EQ(bounded_value, direct_value);
        CHECK_EQ(dynamic_value, direct_value);
        CHECK_EQ(bounded_input.increments, direct_input.increments);
        CHECK_EQ(dynamic_input.increments, direct_input.increments);
    }
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
