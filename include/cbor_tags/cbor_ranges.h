#pragma once

#include "cbor_tags/cbor_concepts.h"

#include <cstddef>
#include <ranges>
#include <type_traits>
#include <utility>

namespace cbor::tags {

template <std::ranges::view R> struct array_range {
    using range_type = R;
    using value_type = std::remove_cvref_t<std::ranges::range_value_t<R>>;

    R range_;

    constexpr explicit array_range(R range) : range_(std::move(range)) {}
};

template <std::ranges::view R> struct map_range {
    using range_type  = R;
    using entry_type  = std::ranges::range_reference_t<R>;
    using key_type    = std::remove_cvref_t<decltype(detail::pair_first(std::declval<entry_type>()))>;
    using mapped_type = std::remove_cvref_t<decltype(detail::pair_second(std::declval<entry_type>()))>;

    R range_;

    constexpr explicit map_range(R range) : range_(std::move(range)) {}
};

template <std::ranges::view R> struct bstr_range {
    using range_type = R;
    using value_type = std::byte;

    R           range_;
    std::size_t chunk_size_{4096};

    constexpr explicit bstr_range(R range, std::size_t chunk_size = 4096) : range_(std::move(range)), chunk_size_(chunk_size) {}
};

template <std::ranges::view R> struct tstr_range {
    using range_type = R;
    using value_type = char;

    R           range_;
    std::size_t chunk_size_{4096};

    constexpr explicit tstr_range(R range, std::size_t chunk_size = 4096) : range_(std::move(range)), chunk_size_(chunk_size) {}
};

namespace detail {

template <typename T> struct is_valid_explicit_range_wrapper : std::false_type {};
template <typename T> struct is_const_iterable_explicit_range_wrapper : std::false_type {};
template <typename T> struct is_array_range_wrapper : std::false_type {};
template <typename T> struct is_map_range_wrapper : std::false_type {};
template <typename T> struct is_bstr_range_wrapper : std::false_type {};
template <typename T> struct is_tstr_range_wrapper : std::false_type {};

template <typename T>
concept ExplicitRangeWrapperReference = is_valid_explicit_range_wrapper<std::remove_cvref_t<T>>::value;

template <typename T>
concept ExplicitRangeWrapperComponent =
    ExplicitRangeWrapperReference<T> &&
    (!std::is_const_v<std::remove_reference_t<T>> || is_const_iterable_explicit_range_wrapper<std::remove_cvref_t<T>>::value);

template <typename T>
concept CborRangeComponent = IsCborMajor<std::remove_cvref_t<T>> || ExplicitRangeWrapperComponent<T>;

template <typename Reference, typename Value>
concept MaterializableCborRangeComponent =
    CborRangeComponent<Reference> ||
    (!ExplicitRangeWrapperReference<Reference> && CborRangeComponent<Value> && std::constructible_from<Value, Reference>);

template <typename R>
concept CborArrayRange =
    std::ranges::input_range<R> && MaterializableCborRangeComponent<std::ranges::range_reference_t<R>, std::ranges::range_value_t<R>>;

template <typename R>
concept CborTextRange = std::ranges::input_range<R> && IsTextChar<std::ranges::range_value_t<R>>;

template <typename T>
concept CborMapEntry = IsPairLike<T> && CborRangeComponent<decltype(pair_first(std::declval<T>()))> &&
                       CborRangeComponent<decltype(pair_second(std::declval<T>()))>;

template <std::ranges::view R> struct is_valid_explicit_range_wrapper<array_range<R>> : std::bool_constant<CborArrayRange<R>> {};
template <std::ranges::view R> struct is_array_range_wrapper<array_range<R>> : std::true_type {};

template <std::ranges::view R>
struct is_valid_explicit_range_wrapper<map_range<R>>
    : std::bool_constant<std::ranges::input_range<R> && IsPairLikeRange<R> && CborMapEntry<std::ranges::range_reference_t<R>>> {};
template <std::ranges::view R> struct is_map_range_wrapper<map_range<R>> : std::true_type {};

template <std::ranges::view R> struct is_valid_explicit_range_wrapper<bstr_range<R>> : std::bool_constant<IsByteLikeRange<R>> {};
template <std::ranges::view R> struct is_bstr_range_wrapper<bstr_range<R>> : std::true_type {};

template <std::ranges::view R> struct is_valid_explicit_range_wrapper<tstr_range<R>> : std::bool_constant<CborTextRange<R>> {};
template <std::ranges::view R> struct is_tstr_range_wrapper<tstr_range<R>> : std::true_type {};

template <std::ranges::view R>
struct is_const_iterable_explicit_range_wrapper<array_range<R>> : std::bool_constant<std::ranges::range<const R>> {};

template <std::ranges::view R>
struct is_const_iterable_explicit_range_wrapper<map_range<R>> : std::bool_constant<std::ranges::range<const R>> {};

template <std::ranges::view R>
struct is_const_iterable_explicit_range_wrapper<bstr_range<R>> : std::bool_constant<std::ranges::range<const R>> {};

template <std::ranges::view R>
struct is_const_iterable_explicit_range_wrapper<tstr_range<R>> : std::bool_constant<std::ranges::range<const R>> {};

template <typename T>
concept ArrayRangeWrapper = is_array_range_wrapper<std::remove_cvref_t<T>>::value;

template <typename T>
concept MapRangeWrapper = is_map_range_wrapper<std::remove_cvref_t<T>>::value;

template <typename T>
concept BstrRangeWrapper = is_bstr_range_wrapper<std::remove_cvref_t<T>>::value;

template <typename T>
concept TstrRangeWrapper = is_tstr_range_wrapper<std::remove_cvref_t<T>>::value;

template <typename T>
concept StringRangeWrapper = BstrRangeWrapper<T> || TstrRangeWrapper<T>;

template <typename T> using explicit_range_reference_t = decltype((std::declval<T>().range_));

template <typename T>
concept SizedExplicitRangeWrapper = ExplicitRangeWrapperComponent<T> && std::ranges::sized_range<explicit_range_reference_t<T>>;

template <typename T>
concept ConstSizedExplicitRangeWrapper = SizedExplicitRangeWrapper<const std::remove_reference_t<T>>;

} // namespace detail

template <std::ranges::viewable_range R>
    requires detail::CborArrayRange<std::views::all_t<R>>
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

template <std::ranges::viewable_range R>
    requires detail::CborTextRange<std::views::all_t<R>>
[[nodiscard]] constexpr auto as_tstr_range(R &&range, std::size_t chunk_size = 4096) {
    return tstr_range<std::views::all_t<R>>{std::views::all(std::forward<R>(range)), chunk_size};
}

} // namespace cbor::tags
