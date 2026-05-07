#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <utility>

namespace cbor::tags::detail {

template <typename T>
concept RangeByteLike =
    std::same_as<std::remove_cvref_t<T>, std::byte> ||
    (std::integral<std::remove_cvref_t<T>> && sizeof(std::remove_cvref_t<T>) == 1 && !std::same_as<std::remove_cvref_t<T>, bool>);

template <typename T>
concept ByteLikeRange =
    std::ranges::input_range<std::remove_cvref_t<T>> && RangeByteLike<std::ranges::range_value_t<std::remove_cvref_t<T>>>;

template <typename T>
concept TuplePairLike = requires {
    typename std::tuple_size<std::remove_cvref_t<T>>::type;
    requires std::tuple_size_v<std::remove_cvref_t<T>> == 2;
} && requires(T &&value) {
    std::get<0>(std::forward<T>(value));
    std::get<1>(std::forward<T>(value));
};

template <typename T>
concept MemberPairLike = requires(T &&value) {
    std::forward<T>(value).first;
    std::forward<T>(value).second;
};

template <typename T>
concept PairLike = TuplePairLike<T> || MemberPairLike<T>;

template <typename T>
concept PairLikeRange =
    std::ranges::input_range<std::remove_cvref_t<T>> && PairLike<std::ranges::range_reference_t<std::remove_cvref_t<T>>>;

template <class T> constexpr bool is_optional_v                   = false;
template <class T> constexpr bool is_optional_v<std::optional<T>> = true;

template <typename T, bool IsStringBase, bool IsOptional>
concept RangeOfCborValuesBase = std::ranges::range<std::remove_cvref_t<T>> && std::is_class_v<std::remove_cvref_t<T>> && !IsStringBase &&
                                !IsOptional;

template <typename T, bool IsRangeOfCborValuesBase>
concept MapLikeContainer = IsRangeOfCborValuesBase && requires(std::remove_cvref_t<T> t) {
    typename std::remove_cvref_t<T>::key_type;
    typename std::remove_cvref_t<T>::mapped_type;
    requires PairLike<std::ranges::range_reference_t<std::remove_cvref_t<T>>>;
    t.find(std::declval<typename std::remove_cvref_t<T>::key_type>());
};

} // namespace cbor::tags::detail
