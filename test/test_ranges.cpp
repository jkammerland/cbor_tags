#include <algorithm>
#include <cbor_tags/cbor.h>
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
