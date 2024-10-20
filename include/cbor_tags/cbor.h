#pragma once

// Float 16, c++23 has std::float16_t from <stdfloat> maybe, for now use float16_t below
#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/float16_ieee754.h"

#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace cbor::tags {

struct binary_array_view {
    std::span<const std::byte> data;
};

struct binary_map_view {
    std::span<const std::byte> data;
};

struct binary_tag_view {
    std::uint64_t              tag;
    std::span<const std::byte> data;
};

template <std::ranges::input_range R> struct binary_range_view {
    R range;

    constexpr auto view() const {
        return range | std::views::transform([](const auto &c) { return static_cast<const std::byte>(c); });
    }
    constexpr auto begin() const {
        auto v = view();
        return std::ranges::begin(v);
    }
    constexpr auto end() const {
        auto v = view();
        return std::ranges::end(v);
    }

    operator std::vector<std::byte>() const { return {range.begin(), range.end()}; }
};

template <std::ranges::input_range R> struct char_range_view {
    R range;

    constexpr auto view() const {
        return range | std::views::transform([](const auto &c) { return static_cast<const char>(c); });
    }

    constexpr auto begin() const {
        auto v = view();
        return std::ranges::begin(v);
    }
    constexpr auto end() const {
        auto v = view();
        return std::ranges::end(v);
    }

    constexpr operator std::string() const { return {begin(), end()}; }
};

template <std::ranges::input_range R> struct binary_array_range_view {
    R range;
};

template <std::ranges::input_range R> struct binary_map_range_view {
    R range;
};

template <std::ranges::input_range R> struct binary_tag_range_view {
    std::uint64_t tag;
    R             range;
};

using variant_contiguous = std::variant<std::uint64_t, std::int64_t, std::span<const std::byte>, std::string_view, binary_array_view,
                                        binary_map_view, binary_tag_view, float16_t, float, double, bool, std::nullptr_t>;

template <typename R>
using variant_ranges = std::variant<std::uint64_t, std::int64_t, binary_range_view<R>, char_range_view<R>, binary_array_range_view<R>,
                                    binary_map_range_view<R>, binary_tag_range_view<R>, float16_t, float, double, bool, std::nullptr_t>;

template <typename T> using subrange  = std::ranges::subrange<typename detail::iterator_type<T>::type>;
template <typename T> using variant_t = std::conditional_t<IsContiguous<T>, variant_contiguous, variant_ranges<subrange<T>>>;

template <typename Tag, typename T> using tag_pair = std::pair<Tag, T>;
template <typename Tag, typename T> constexpr auto make_tag_pair(Tag t, T &&value) { return tag_pair<Tag, T>{t, std::forward<T>(value)}; }

} // namespace cbor::tags