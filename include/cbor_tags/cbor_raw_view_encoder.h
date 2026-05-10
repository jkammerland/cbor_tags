#pragma once

#include "cbor_tags/cbor_detail.h"
#include "cbor_tags/cbor_raw_views.h"

namespace cbor::tags {

template <typename T> struct cbor_raw_view_encoder {
    template <typename RawView>
        requires IsEncodedItemView<RawView>
    constexpr void encode_encoded_view(const RawView &value) {
        auto &enc = detail::underlying<T>(this);
        detail::append_byte_range(enc.appender_, enc.data_, value.bytes());
    }

    template <typename R> constexpr void encode(const basic_encoded_item_view<R> &value) { encode_encoded_view(value); }
    template <typename R> constexpr void encode(const basic_encoded_array_view<R> &value) { encode_encoded_view(value); }
    template <typename R> constexpr void encode(const basic_encoded_map_view<R> &value) { encode_encoded_view(value); }
};

} // namespace cbor::tags
