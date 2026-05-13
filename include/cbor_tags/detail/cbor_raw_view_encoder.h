#pragma once

#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_raw_views.h"

#include <concepts>
#include <ranges>
#include <span>
#include <type_traits>

namespace cbor::tags::detail {

template <typename T> struct cbor_raw_view_encoder {
    template <typename RawView>
        requires IsEncodedItemView<RawView>
    constexpr void encode_encoded_view(const RawView &value) {
        auto &enc        = underlying<T>(this);
        using range_type = typename std::remove_cvref_t<RawView>::range_type;
        if constexpr (std::ranges::borrowed_range<range_type> && requires {
                          { value.span() } -> std::same_as<std::span<const std::byte>>;
                          enc.appender_.append_borrowed(enc.data_, value.span());
                      }) {
            enc.appender_.append_borrowed(enc.data_, value.span());
        } else {
            append_byte_range(enc.appender_, enc.data_, value.bytes());
        }
    }

    template <typename R> constexpr void encode(const basic_encoded_item_view<R> &value) { encode_encoded_view(value); }
    template <typename R> constexpr void encode(const basic_encoded_array_view<R> &value) { encode_encoded_view(value); }
    template <typename R> constexpr void encode(const basic_encoded_map_view<R> &value) { encode_encoded_view(value); }
};

} // namespace cbor::tags::detail
