#pragma once

#include "cbor_tags/cbor_concepts.h"

#include <cstddef>
#include <ranges>
#include <type_traits>
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

namespace detail {

template <typename T> struct is_explicit_range_wrapper : std::false_type {};
template <std::ranges::view R> struct is_explicit_range_wrapper<array_range<R>> : std::true_type {};
template <std::ranges::view R> struct is_explicit_range_wrapper<map_range<R>> : std::true_type {};
template <std::ranges::view R> struct is_explicit_range_wrapper<bstr_range<R>> : std::true_type {};
template <typename T> constexpr bool is_explicit_range_wrapper_v = is_explicit_range_wrapper<std::remove_cvref_t<T>>::value;

template <typename T>
concept CborMapEntryComponent = IsCborMajor<std::remove_cvref_t<T>> || is_explicit_range_wrapper_v<T>;

template <typename T>
concept CborMapEntry = IsPairLike<T> && CborMapEntryComponent<decltype(pair_first(std::declval<T>()))> &&
                       CborMapEntryComponent<decltype(pair_second(std::declval<T>()))>;

} // namespace detail

template <std::ranges::viewable_range R>
    requires IsCborMajor<std::ranges::range_value_t<std::views::all_t<R>>>
[[nodiscard]] constexpr auto as_array_range(R &&range) {
    return array_range<std::views::all_t<R>>{std::views::all(std::forward<R>(range))};
}

template <std::ranges::viewable_range R>
    requires IsPairLikeRange<std::views::all_t<R>> && detail::CborMapEntry<std::ranges::range_reference_t<std::views::all_t<R>>>
[[nodiscard]] constexpr auto as_map_range(R &&range) {
    return map_range<std::views::all_t<R>>{std::views::all(std::forward<R>(range))};
}

template <std::ranges::viewable_range R>
    requires IsByteLikeRange<std::views::all_t<R>>
[[nodiscard]] constexpr auto as_bstr_range(R &&range, std::size_t chunk_size = 4096) {
    return bstr_range<std::views::all_t<R>>{std::views::all(std::forward<R>(range)), chunk_size};
}

} // namespace cbor::tags
