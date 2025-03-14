#include "cbor_tags/cbor_ranges.h"
#include "test_util.h"

#include <algorithm>
#include <cbor_tags/cbor.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <fmt/base.h>
#include <iomanip>
#include <ranges>
#include <string>
#include <utility>

using namespace cbor::tags;

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

    // Method 1: Using ranges::join
    auto joined_view = std::vector<std::ranges::ref_view<std::vector<char>>>{std::ranges::ref_view(buffer1), std::ranges::ref_view(buffer2),
                                                                             std::ranges::ref_view(buffer3)} |
                       std::ranges::views::join;

    fmt::print("All data joined: {}\n", to_hex(joined_view));

    // Method 2: Process each buffer separately
    std::vector<std::reference_wrapper<std::vector<char>>> buffers = {buffer1, buffer2, buffer3};

    fmt::print("\nEach buffer separately:\n");
    for (auto &buffer : buffers) {
        fmt::print("Buffer: {}\n", to_hex(buffer.get()));
    }
}

TEST_CASE("joining views of different types") {
    // Example buffers of different types
    std::vector<char>   char_buffer   = {0x01, 0x02, 0x03};
    std::string         string_buffer = "Hello";
    std::array<char, 4> array_buffer  = {'W', 'o', 'r', 'l'};

    // Print individual buffers
    fmt::print("Vector buffer: {}\n", to_hex(char_buffer));
    fmt::print("String buffer: {}\n", to_hex(string_buffer));
    fmt::print("Array buffer: {}\n", to_hex(array_buffer));

    // Method 1: Concatenate the hex strings
    std::string combined_hex = to_hex(char_buffer) + to_hex(string_buffer) + to_hex(array_buffer);
    fmt::print("\nConcatenated hex: {}\n", combined_hex);

    // Method 2: Using a tuple and transforming each element
    auto buffers_tuple =
        std::make_tuple(std::ranges::ref_view(char_buffer), std::ranges::ref_view(string_buffer), std::ranges::ref_view(array_buffer));

    std::string tuple_hex;
    std::apply([&](const auto &...views) { ((tuple_hex += to_hex(views)), ...); }, buffers_tuple);

    fmt::print("Tuple-joined hex: {}\n", tuple_hex);

    // Method 3: Create a custom view that concatenates iterators
    auto char_view   = std::ranges::ref_view(char_buffer);
    auto string_view = std::ranges::ref_view(string_buffer);
    auto array_view  = std::ranges::ref_view(array_buffer);
    auto deq         = std::deque<char>{static_cast<char>(1), static_cast<char>(2)};
    auto deq_view    = std::ranges::ref_view(deq);

    auto        concatenated_view = concat(char_view, string_view, array_view, deq_view);
    std::string joined_hex;
    for (const auto &byte : concatenated_view) {
        joined_hex += fmt::format("{:02x}", byte);
    }
    fmt::print("Joined hex: {}\n", joined_hex);
}

TEST_CASE("testing concat_view implementation") {
    // Test data
    std::vector<char>   vec = {0x01, 0x02, 0x03};
    std::string         str = "Hello";
    std::array<char, 4> arr = {'W', 'o', 'r', 'l'};
    std::deque<char>    deq = {0x0A, 0x0B};

    // Create the expected result for comparison
    std::vector<char> expected;
    expected.insert(expected.end(), vec.begin(), vec.end()); // 01 02 03
    expected.insert(expected.end(), str.begin(), str.end()); // 48 65 6c 6c 6f
    expected.insert(expected.end(), arr.begin(), arr.end()); // 57 6f 72 6c
    expected.insert(expected.end(), deq.begin(), deq.end()); // 0A 0B

    SUBCASE("Concatenating multiple ranges") {
        auto concatenated =
            concat(std::ranges::ref_view(vec), std::ranges::ref_view(str), std::ranges::ref_view(arr), std::ranges::ref_view(deq));

        // Verify contents match expected
        std::vector<char> actual(concatenated.begin(), concatenated.end());
        CHECK(actual == expected);

        // Verify size
        CHECK(actual.size() == expected.size());
        CHECK(actual.size() == 14); // 3 + 5 + 4 + 2
    }

    SUBCASE("Handling empty ranges") {
        std::vector<char> empty;

        auto with_empty = concat(std::ranges::ref_view(empty), std::ranges::ref_view(vec), std::ranges::ref_view(empty),
                                 std::ranges::ref_view(str), std::ranges::ref_view(arr), std::ranges::ref_view(deq));

        std::vector<char> actual(with_empty.begin(), with_empty.end());
        CHECK(actual == expected);

        auto only_empty = concat(std::ranges::ref_view(empty), std::ranges::ref_view(empty));

        std::vector<char> empty_result(only_empty.begin(), only_empty.end());
        CHECK(empty_result.empty());
    }

    SUBCASE("Reflecting source range modifications") {
        auto concatenated = concat(std::ranges::ref_view(vec), std::ranges::ref_view(str));

        // Save initial state
        std::vector<char> before(concatenated.begin(), concatenated.end());

        // Modify source ranges
        vec[0] = 0xFF;
        str[0] = 'X';

        // Verify changes are reflected
        std::vector<char> after(concatenated.begin(), concatenated.end());

        CHECK(after[0] == 0xFF);
        CHECK(after[3] == 'X'); // First char of string at index 3
        CHECK(before != after);
    }

    SUBCASE("Correct iteration across range boundaries") {
        auto concatenated = concat(std::ranges::ref_view(vec), std::ranges::ref_view(str));

        auto it = concatenated.begin();

        // Iterate to the boundary
        for (size_t i = 0; i < vec.size(); ++i) {
            CHECK(*it == vec[i]);
            ++it;
        }

        // Now we should be at the start of the string
        for (size_t i = 0; i < str.size(); ++i) {
            CHECK(*it == str[i]);
            ++it;
        }

        // Should now be at the end
        CHECK(it == concatenated.end());
    }

    SUBCASE("to_hex validation for concat") {
        auto concatenated =
            concat(std::ranges::ref_view(vec), std::ranges::ref_view(str), std::ranges::ref_view(arr), std::ranges::ref_view(deq));

        std::string hex_result   = to_hex(concatenated);
        std::string expected_hex = to_hex(expected);

        CHECK(hex_result == expected_hex);

        // Manual validation of hex output
        std::string manual_hex;
        for (char c : concatenated) {
            manual_hex += fmt::format("{:02x}", static_cast<unsigned char>(c));
        }
        CHECK(manual_hex == expected_hex);
    }
}
