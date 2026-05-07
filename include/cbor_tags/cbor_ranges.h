#pragma once

#include "cbor_tags/cbor_concepts.h"

#include <cstddef>
#include <ranges>
#include <utility>

namespace cbor::tags {

template <std::ranges::view R> struct array_range {
    R range_;

    constexpr explicit array_range(R range) : range_(std::move(range)) {}
};

template <std::ranges::view R> struct map_range {
    R range_;

    constexpr explicit map_range(R range) : range_(std::move(range)) {}
};

template <std::ranges::view R> struct bstr_range {
    R           range_;
    std::size_t chunk_size_{4096};

    constexpr explicit bstr_range(R range, std::size_t chunk_size = 4096) : range_(std::move(range)), chunk_size_(chunk_size) {}
};

template <std::ranges::viewable_range R>
    requires IsCborMajor<std::ranges::range_value_t<std::views::all_t<R>>>
[[nodiscard]] constexpr auto as_array_range(R &&range) {
    return array_range<std::views::all_t<R>>{std::views::all(std::forward<R>(range))};
}

template <std::ranges::viewable_range R>
    requires IsPairLikeRange<std::views::all_t<R>>
[[nodiscard]] constexpr auto as_map_range(R &&range) {
    return map_range<std::views::all_t<R>>{std::views::all(std::forward<R>(range))};
}

template <std::ranges::viewable_range R>
    requires IsByteLikeRange<std::views::all_t<R>>
[[nodiscard]] constexpr auto as_bstr_range(R &&range, std::size_t chunk_size = 4096) {
    return bstr_range<std::views::all_t<R>>{std::views::all(std::forward<R>(range)), chunk_size};
}

} // namespace cbor::tags
