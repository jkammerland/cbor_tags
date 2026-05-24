#include "test_util.h"

#include <algorithm>
#include <array>
#include <cbor_tags/cbor.h>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_ranges.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <fmt/base.h>
#include <functional>
#include <iomanip>
#include <memory>
#include <ranges>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#if __has_include(<boost/container/flat_map.hpp>) && __has_include(<boost/container/list.hpp>) && __has_include(<boost/container/small_vector.hpp>) && \
    __has_include(<boost/container/static_vector.hpp>) && __has_include(<boost/container/string.hpp>) && __has_include(<boost/container/vector.hpp>)
#include <boost/container/flat_map.hpp>
#include <boost/container/list.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/container/static_vector.hpp>
#include <boost/container/string.hpp>
#include <boost/container/vector.hpp>
#define CBOR_TAGS_HAS_BOOST_CONTAINER_RANGES 1
#endif

#if __has_include(<boost/container/flat_map.hpp>) && __has_include(<boost/container/map.hpp>) && \
    __has_include(<boost/unordered/unordered_map.hpp>)
#include <boost/container/flat_map.hpp>
#include <boost/container/map.hpp>
#include <boost/unordered/unordered_map.hpp>
#define CBOR_TAGS_HAS_BOOST_MAPS 1
#endif

using namespace cbor::tags;

namespace {

struct member_pair_entry {
    int first;
    int second;
};

struct range_not_cbor {};

template <typename R>
concept CanMakeMapRange = requires(R &&range) { as_map_range(std::forward<R>(range)); };

} // namespace

static_assert(IsPairLike<std::pair<int, int>>);
static_assert(IsPairLike<std::tuple<int, int>>);
static_assert(IsPairLike<std::array<int, 2>>);
static_assert(IsPairLike<member_pair_entry>);
static_assert(!IsPairLike<std::tuple<int>>);
static_assert(!IsPairLike<std::tuple<int, int, int>>);
static_assert(!IsPairLike<std::array<int, 3>>);
static_assert(!CanMakeMapRange<std::array<std::tuple<int, int, int>, 1> &>);
static_assert(!CanMakeMapRange<std::array<std::pair<int, range_not_cbor>, 1> &>);
static_assert(!CanMakeMapRange<std::array<std::pair<range_not_cbor, int>, 1> &>);
static_assert(std::is_same_v<decltype(detail::pair_first(std::declval<member_pair_entry &>())), int &>);
static_assert(std::is_same_v<decltype(detail::pair_second(std::declval<const member_pair_entry &>())), const int &>);

TEST_CASE("Test ranges 1") {
    // Create a deque of chars (non-contiguous in memory)
    std::deque<char> char_deque{'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd'};

    // Create a view of the entire deque (a borrowed range)
    auto full_view = std::views::all(char_deque);

    // Convert to vector for comparison
    std::vector<char> result(full_view.begin(), full_view.end());
    std::vector<char> expected{'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd'};

    CHECK(result == expected);
}

TEST_CASE("Test ranges 2") {
    std::deque<char> char_deque{'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd'};

    // First 5 characters ("Hello")
    auto        hello_view = char_deque | std::views::take(5);
    std::string hello_result(hello_view.begin(), hello_view.end());
    CHECK(hello_result == "Hello");

    // Skip first 6 characters to get "World"
    auto        world_view = char_deque | std::views::drop(6);
    std::string world_result(world_view.begin(), world_view.end());
    CHECK(world_result == "World");

    // Characters at positions 2-7 ("llo Wo")
    auto        range_view = char_deque | std::views::drop(2) | std::views::take(6);
    std::string range_result(range_view.begin(), range_view.end());
    CHECK(range_result == "llo Wo");

    // Only lowercase letters
    auto        lowercase_view = char_deque | std::views::filter([](char c) { return c >= 'a' && c <= 'z'; });
    std::string lowercase_result(lowercase_view.begin(), lowercase_view.end());
    CHECK(lowercase_result == "elloorld");
}

TEST_CASE("Transforming view 1") {
    std::deque<char> char_deque{'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd'};

    // Convert to uppercase
    auto uppercase_view =
        char_deque | std::views::transform([](char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; });

    // Check all uppercase
    std::string uppercase_result(uppercase_view.begin(), uppercase_view.end());
    CHECK(uppercase_result == "HELLO WORLD");
}

template <typename Iter1, typename Iter2> class zip_iterator {
  private:
    Iter1 it1;
    Iter2 it2;

  public:
    zip_iterator(Iter1 it1, Iter2 it2) : it1(std::move(it1)), it2(std::move(it2)) {}

    auto operator*() const { return std::make_pair(*it1, *it2); }

    zip_iterator &operator++() {
        ++it1;
        ++it2;
        return *this;
    }

    bool operator!=(const zip_iterator &other) const { return it1 != other.it1 && it2 != other.it2; }
};

template <typename Range1, typename Range2> class zip_container {
  private:
    Range1 &range1;
    Range2 &range2;

    using iter1_t = decltype(std::begin(std::declval<Range1 &>()));
    using iter2_t = decltype(std::begin(std::declval<Range2 &>()));

  public:
    zip_container(Range1 &r1, Range2 &r2) : range1(r1), range2(r2) {}

    auto begin() { return zip_iterator<iter1_t, iter2_t>(std::begin(range1), std::begin(range2)); }
    auto end() { return zip_iterator<iter1_t, iter2_t>(std::end(range1), std::end(range2)); }
};
TEST_CASE("Transforming view 2 - to std::byte view") {
    std::deque<char> char_deque{'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd'};

    // Convert to std::byte view
    auto byte_view = char_deque | std::views::transform([](char c) { return static_cast<std::byte>(c); });

    // Check all bytes are correct
    for (auto [original, transformed] : zip_container(char_deque, byte_view)) {
        CHECK(static_cast<char>(transformed) == original);
    }
}

TEST_CASE("Transforming view 3 - wrapper_view") {
    using char_iterator_t = std::ranges::iterator_t<std::deque<char>>;
    using subr            = std::ranges::subrange<char_iterator_t>;

    std::deque<char> char_deque{'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd'};

    // Create a subrange
    subr hello_subrange{char_deque.begin(), char_deque.begin() + 5};

    // Apply multiple transformations
    auto complex_view = hello_subrange | std::views::drop(1) // Skip 'H'
                        | std::views::take(3)                // Take 'ell'
                        | std::views::transform([](char c) { return std::toupper(c); });

    REQUIRE(complex_view.size() == 3);
    CHECK_EQ(std::string{complex_view.begin(), complex_view.end()}, "ELL");
}

namespace actions {
struct sort_fn {
    template <std::ranges::random_access_range R> auto operator()(R &&r) const -> decltype(auto) {
        std::ranges::sort(r);
        return std::forward<R>(r);
    }

    // Pipeline syntax support
    template <std::ranges::random_access_range R> friend auto operator|(R &&r, const sort_fn &sorter) { return sorter(std::forward<R>(r)); }
};

inline constexpr sort_fn sort{};

// Another example: an action that squares elements in place
struct square_in_place_fn {
    template <std::ranges::forward_range R>
        requires std::ranges::output_range<R, std::ranges::range_value_t<R>>
    auto operator()(R &&r) const -> decltype(auto) {
        for (auto &elem : r) {
            elem = elem * elem;
        }
        return std::forward<R>(r);
    }

    // Pipeline syntax support
    template <std::ranges::forward_range R> friend auto operator|(R &&r, const square_in_place_fn &squarer) {
        return squarer(std::forward<R>(r));
    }
};

inline constexpr square_in_place_fn square_in_place{};
} // namespace actions

// Usage
TEST_CASE("Custom Actions") {
    std::vector<int> vec = {5, 3, 1, 4, 2};

    // Use our custom actions with pipeline syntax
    vec = vec | actions::square_in_place | actions::sort;

    CHECK(vec == (std::vector{1, 4, 9, 16, 25}));
}

TEST_CASE("view joining of multiple buffers") {
    // Example buffers
    std::vector<char> buffer1 = {0x01, 0x02, 0x03, 0x04};
    std::vector<char> buffer2 = {0x05, 0x06, 0x07};
    std::vector<char> buffer3 = {0x08, 0x09, 0x0A, 0x0B, 0x0C};

    // Method 1: Using std::views::join
    auto buffer_views = std::array{std::ranges::ref_view(buffer1), std::ranges::ref_view(buffer2), std::ranges::ref_view(buffer3)};
    auto joined_view  = buffer_views | std::views::join;

    CBOR_TAGS_TEST_LOG("All data joined: {}\n", to_hex(joined_view));

    // Method 2: Process each buffer separately
    std::vector<std::reference_wrapper<std::vector<char>>> buffers = {buffer1, buffer2, buffer3};

    CBOR_TAGS_TEST_LOG("\nEach buffer separately:\n");
    for (auto &buffer : buffers) {
        CBOR_TAGS_TEST_LOG("Buffer: {}\n", to_hex(buffer.get()));
    }

    CHECK_EQ(to_hex(joined_view), "0102030405060708090a0b0c");
}

TEST_CASE("joining views of different types") {
    // Example buffers of different types
    std::vector<char>   char_buffer   = {0x01, 0x02, 0x03};
    std::string         string_buffer = "Hello";
    std::array<char, 4> array_buffer  = {'W', 'o', 'r', 'l'};

    // Print individual buffers
    CBOR_TAGS_TEST_LOG("Vector buffer: {}\n", to_hex(char_buffer));
    CBOR_TAGS_TEST_LOG("String buffer: {}\n", to_hex(string_buffer));
    CBOR_TAGS_TEST_LOG("Array buffer: {}\n", to_hex(array_buffer));

    // # 1.
    auto buffers_tuple =
        std::make_tuple(std::ranges::ref_view(char_buffer), std::ranges::ref_view(string_buffer), std::ranges::ref_view(array_buffer));

    std::string tuple_hex;
    std::apply([&](const auto &...views) { ((tuple_hex += to_hex(views)), ...); }, buffers_tuple);

    CBOR_TAGS_TEST_LOG("Tuple-joined hex: {}\n", tuple_hex);

    // # 2.
    auto char_view   = std::ranges::ref_view(char_buffer);
    auto string_view = std::ranges::ref_view(string_buffer);
    auto array_view  = std::ranges::ref_view(array_buffer);
    auto deq         = std::deque<char>{static_cast<char>(1), static_cast<char>(2)};
    auto deq_view    = std::ranges::ref_view(deq);

    auto        views_tuple = std::make_tuple(char_view, string_view, array_view, deq_view);
    auto        join_hex    = [](const auto &...views) { return (std::string{} + ... + to_hex(views)); };
    std::string joined_hex  = std::apply(join_hex, views_tuple);
    CBOR_TAGS_TEST_LOG("Joined hex: {}\n", joined_hex);

    // Check joined content
    CHECK_EQ(joined_hex, "01020348656c6c6f576f726c0102");
    CHECK_EQ(joined_hex.size(), 28); // 14 characters * 2 hex digits

    // Check size of each component in the concatenated view
    CHECK_EQ(std::ranges::size(char_view), 3);
    CHECK_EQ(std::ranges::size(string_view), 5);
    CHECK_EQ(std::ranges::size(array_view), 4);
    CHECK_EQ(std::ranges::size(deq_view), 2);
    CHECK_EQ(std::ranges::size(char_view) + std::ranges::size(string_view) + std::ranges::size(array_view) + std::ranges::size(deq_view),
             14);

    CHECK_EQ(static_cast<int>(*char_view.begin()), 1);
    CHECK_EQ(static_cast<int>(*std::ranges::prev(deq_view.end())), 2);

    // Modify the original buffers and check that views reflect the changes
    char_buffer[0]   = static_cast<char>(0xFF);
    string_buffer[0] = 'h';

    joined_hex = std::apply(join_hex, views_tuple);
    CHECK_NE(joined_hex, "01020348656c6c6f576f726c0102"); // Should be different after modification
    CHECK_EQ(joined_hex, "ff020368656c6c6f576f726c0102");
}

TEST_CASE("explicit array range wrappers encode sized and non-sized views") {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);

    auto sized = std::views::iota(1, 4);
    REQUIRE(enc(as_array_range(sized)));
    CHECK_EQ(to_hex(buffer), "83010203");

    buffer.clear();
    auto evens = std::views::iota(0, 6) | std::views::filter([](int value) { return value % 2 == 0; });
    REQUIRE(enc(as_array_range(evens)));
    CHECK_EQ(to_hex(buffer), "9f000204ff");

    buffer.clear();
    REQUIRE(enc(as_array_range(std::vector<int>{1, 2, 3})));
    CHECK_EQ(to_hex(buffer), "83010203");

    buffer.clear();
    auto wrapped = as_array_range(std::vector<int>{4, 5});
    REQUIRE(enc(wrapped));
    CHECK_EQ(to_hex(buffer), "820405");
}

TEST_CASE("manual encoder aliases retain range wrapper support") {
    std::vector<std::byte> buffer;
    encoder<std::vector<std::byte>, Options<default_expected, default_wrapping>, cbor_header_encoder, cbor_indefinite_encoder,
            cbor_optional_encoder, cbor_variant_encoder>
        enc{buffer};

    auto values = std::views::iota(1, 4);
    REQUIRE(enc(as_array_range(values)));
    CHECK_EQ(to_hex(buffer), "83010203");
}

TEST_CASE("explicit map range wrappers encode transformed pair views") {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);

    auto sized_pairs = std::views::iota(1, 4) | std::views::transform([](int value) { return std::pair{value, value + 10}; });
    REQUIRE(enc(as_map_range(sized_pairs)));
    CHECK_EQ(to_hex(buffer), "a3010b020c030d");

    buffer.clear();
    auto odd_pairs = std::views::iota(0, 5) | std::views::filter([](int value) { return value % 2 == 1; }) |
                     std::views::transform([](int value) { return std::pair{value, value * 10}; });
    REQUIRE(enc(as_map_range(odd_pairs)));
    CHECK_EQ(to_hex(buffer), "bf010a03181eff");

    buffer.clear();
    REQUIRE(enc(as_map_range(std::vector<std::pair<int, int>>{{1, 2}, {3, 4}})));
    CHECK_EQ(to_hex(buffer), "a201020304");

    buffer.clear();
    auto tuple_pairs = std::array{std::tuple{1, 2}, std::tuple{3, 4}};
    REQUIRE(enc(as_map_range(tuple_pairs)));
    CHECK_EQ(to_hex(buffer), "a201020304");

    buffer.clear();
    auto member_pairs = std::array{member_pair_entry{1, 2}, member_pair_entry{3, 4}};
    REQUIRE(enc(as_map_range(member_pairs)));
    CHECK_EQ(to_hex(buffer), "a201020304");

    buffer.clear();
    std::vector<int> nested_values{1, 2};
    auto             nested_entries = std::array{std::pair{1, as_array_range(nested_values)}};
    static_assert(CanMakeMapRange<decltype(nested_entries) &>);
    REQUIRE(enc(as_map_range(nested_entries)));
    CHECK_EQ(to_hex(buffer), "a101820102");
}

TEST_CASE("explicit byte string range wrappers encode byte-like views") {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);

    auto sized_bytes = std::views::iota(1, 4) | std::views::transform([](int value) { return static_cast<std::uint8_t>(value); });
    REQUIRE(enc(as_bstr_range(sized_bytes)));
    CHECK_EQ(to_hex(buffer), "43010203");

    buffer.clear();
    REQUIRE(enc(as_bstr_range(std::vector<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}}, 2)));
    CHECK_EQ(to_hex(buffer), "43010203");

    buffer.clear();
    auto chunked_bytes = std::views::iota(0, 5) | std::views::filter([](int) { return true; }) |
                         std::views::transform([](int value) { return static_cast<std::byte>(value); });
    REQUIRE(enc(as_bstr_range(chunked_bytes, 2)));
    CHECK_EQ(to_hex(buffer), "5f4200014202034104ff");
}

#ifdef CBOR_TAGS_HAS_BOOST_CONTAINER_RANGES
TEST_CASE("boost containers classify as maps, arrays, text, and explicit bstr ranges") {
    static_assert(IsMap<boost::container::flat_map<int, int>>);
    static_assert(!IsArray<boost::container::flat_map<int, int>>);
    static_assert(IsArray<boost::container::vector<int>>);
    static_assert(IsArray<boost::container::list<int>>);
    static_assert(IsArray<boost::container::small_vector<int, 4>>);
    static_assert(IsArray<boost::container::static_vector<int, 4>>);
    static_assert(IsTextString<boost::container::string>);

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);

    boost::container::flat_map<int, int> flat_map{{1, 2}, {3, 4}};
    REQUIRE(enc(flat_map));
    CHECK_EQ(to_hex(buffer), "a201020304");

    buffer.clear();
    boost::container::vector<std::uint8_t> bytes{1, 2, 3};
    REQUIRE(enc(as_bstr_range(bytes)));
    CHECK_EQ(to_hex(buffer), "43010203");
}
#endif

#ifdef CBOR_TAGS_HAS_BOOST_MAPS
TEST_CASE("boost map containers decode from cbor maps") {
    auto bytes = to_bytes("a201020304");

    {
        auto                            dec = make_decoder(bytes);
        boost::container::map<int, int> decoded;
        auto                            result = dec(decoded);
        REQUIRE(result);
        CHECK_EQ(decoded.at(1), 2);
        CHECK_EQ(decoded.at(3), 4);
    }

    {
        auto                                 dec = make_decoder(bytes);
        boost::container::flat_map<int, int> decoded;
        auto                                 result = dec(decoded);
        REQUIRE(result);
        CHECK_EQ(decoded.at(1), 2);
        CHECK_EQ(decoded.at(3), 4);
    }

    {
        auto                           dec = make_decoder(bytes);
        boost::unordered_map<int, int> decoded;
        auto                           result = dec(decoded);
        REQUIRE(result);
        CHECK_EQ(decoded.at(1), 2);
        CHECK_EQ(decoded.at(3), 4);
    }
}
#endif
