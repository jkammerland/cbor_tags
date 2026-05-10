#pragma once

#include "cbor_tags/cbor_concepts.h"

#include <concepts>
#include <cstddef>
#include <optional>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

namespace cbor::tags {

namespace detail {

template <typename R>
concept EncodedByteViewRange = std::ranges::view<R> && std::ranges::input_range<const R> && std::ranges::sized_range<const R> &&
                               std::ranges::common_range<const R> && IsByteLikeRange<const R>;

} // namespace detail

template <std::ranges::view R>
    requires detail::EncodedByteViewRange<R>
class basic_encoded_byte_view {
  public:
    using range_type = R;
    using size_type  = std::size_t;

    constexpr basic_encoded_byte_view()
        requires std::default_initializable<R>
    = default;

    constexpr explicit basic_encoded_byte_view(R bytes) : bytes_(std::move(bytes)) {}

    [[nodiscard]] constexpr auto begin() const noexcept(noexcept(std::ranges::begin(bytes_))) { return std::ranges::begin(bytes_); }
    [[nodiscard]] constexpr auto end() const noexcept(noexcept(std::ranges::end(bytes_))) { return std::ranges::end(bytes_); }

    [[nodiscard]] constexpr size_type size() const noexcept(noexcept(std::ranges::size(bytes_))) {
        return static_cast<size_type>(std::ranges::size(bytes_));
    }

    [[nodiscard]] constexpr std::optional<std::span<const std::byte>> span() const noexcept {
        if constexpr (std::ranges::contiguous_range<const R> && std::ranges::sized_range<const R>) {
            auto values = std::span{std::ranges::data(bytes_), static_cast<std::size_t>(std::ranges::size(bytes_))};
            return std::as_bytes(values);
        } else {
            return std::nullopt;
        }
    }

  private:
    R bytes_{};
};

using encoded_byte_view = basic_encoded_byte_view<std::span<const std::byte>>;

template <std::ranges::view R>
    requires detail::EncodedByteViewRange<R>
class basic_encoded_item_view {
  public:
    using range_type     = R;
    using byte_view_type = basic_encoded_byte_view<R>;

    constexpr basic_encoded_item_view() = default;
    constexpr explicit basic_encoded_item_view(byte_view_type bytes) : bytes_(std::move(bytes)) {}
    constexpr explicit basic_encoded_item_view(R bytes) : bytes_(byte_view_type{std::move(bytes)}) {}

    [[nodiscard]] constexpr byte_view_type                            bytes() const { return bytes_; }
    [[nodiscard]] constexpr std::optional<std::span<const std::byte>> span() const noexcept { return bytes_.span(); }
    [[nodiscard]] constexpr std::size_t                               size() const noexcept { return bytes_.size(); }

  private:
    byte_view_type bytes_{};
};

template <std::ranges::view R>
    requires detail::EncodedByteViewRange<R>
class basic_encoded_array_view : public basic_encoded_item_view<R> {
  public:
    using basic_encoded_item_view<R>::basic_encoded_item_view;
};

template <std::ranges::view R>
    requires detail::EncodedByteViewRange<R>
class basic_encoded_map_view : public basic_encoded_item_view<R> {
  public:
    using basic_encoded_item_view<R>::basic_encoded_item_view;
};

using encoded_item_view  = basic_encoded_item_view<std::span<const std::byte>>;
using encoded_array_view = basic_encoded_array_view<std::span<const std::byte>>;
using encoded_map_view   = basic_encoded_map_view<std::span<const std::byte>>;

template <typename Buffer>
using encoded_subrange_for =
    std::ranges::subrange<std::ranges::iterator_t<const std::remove_reference_t<Buffer>>,
                          std::ranges::iterator_t<const std::remove_reference_t<Buffer>>, std::ranges::subrange_kind::sized>;

template <typename Buffer> using encoded_byte_view_for  = basic_encoded_byte_view<encoded_subrange_for<Buffer>>;
template <typename Buffer> using encoded_item_view_for  = basic_encoded_item_view<encoded_subrange_for<Buffer>>;
template <typename Buffer> using encoded_array_view_for = basic_encoded_array_view<encoded_subrange_for<Buffer>>;
template <typename Buffer> using encoded_map_view_for   = basic_encoded_map_view<encoded_subrange_for<Buffer>>;

template <typename T> struct is_encoded_item_view : std::false_type {};
template <typename R> struct is_encoded_item_view<basic_encoded_item_view<R>> : std::true_type {};
template <typename R> struct is_encoded_item_view<basic_encoded_array_view<R>> : std::true_type {};
template <typename R> struct is_encoded_item_view<basic_encoded_map_view<R>> : std::true_type {};

template <typename T>
concept IsEncodedItemView = is_encoded_item_view<std::remove_cvref_t<T>>::value;

template <typename T> struct is_encoded_array_view : std::false_type {};
template <typename R> struct is_encoded_array_view<basic_encoded_array_view<R>> : std::true_type {};

template <typename T>
concept IsEncodedArrayView = is_encoded_array_view<std::remove_cvref_t<T>>::value;

template <typename T> struct is_encoded_map_view : std::false_type {};
template <typename R> struct is_encoded_map_view<basic_encoded_map_view<R>> : std::true_type {};

template <typename T>
concept IsEncodedMapView = is_encoded_map_view<std::remove_cvref_t<T>>::value;

} // namespace cbor::tags

template <typename R>
    requires cbor::tags::detail::EncodedByteViewRange<R>
inline constexpr bool std::ranges::enable_view<cbor::tags::basic_encoded_byte_view<R>> = true;

template <typename R>
    requires cbor::tags::detail::EncodedByteViewRange<R>
inline constexpr bool std::ranges::enable_borrowed_range<cbor::tags::basic_encoded_byte_view<R>> = std::ranges::borrowed_range<R>;
