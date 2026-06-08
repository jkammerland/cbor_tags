#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/detail/cbor_item.h"

#include <cstddef>
#include <limits>
#include <optional>
#include <ranges>
#include <utility>

namespace cbor::tags::detail {

template <typename Iterator, typename SizeType> struct raw_encoded_item_bounds {
    Iterator start{};
    Iterator cursor{};
    SizeType size{};
};

template <typename InputBuffer, typename SizeType, std::size_t MaxDepth = default_max_decode_depth>
constexpr status_code read_raw_encoded_item_bounds(const InputBuffer &data, std::ranges::iterator_t<const InputBuffer> start,
                                                   std::optional<major_type> expected_major, status_code major_mismatch,
                                                   raw_encoded_item_bounds<std::ranges::iterator_t<const InputBuffer>, SizeType> &bounds) {
    const auto end = std::ranges::end(data);

    auto             header_cursor = start;
    cbor_item_header header{};
    auto             status = status_code::success;
    if (!cbor_item_reader::read_header(header_cursor, end, header, status)) {
        return status;
    }
    if (expected_major && header.major != *expected_major) {
        return major_mismatch;
    }

    auto cursor = start;
    if (!cbor_item_skipper<MaxDepth>::skip_item(cursor, end, status)) {
        return status;
    }

    const auto distance = std::ranges::distance(start, cursor);
    if (distance < 0 || std::cmp_greater(distance, std::numeric_limits<SizeType>::max())) {
        return status_code::error;
    }

    bounds = raw_encoded_item_bounds<std::ranges::iterator_t<const InputBuffer>, SizeType>{
        .start  = start,
        .cursor = cursor,
        .size   = static_cast<SizeType>(distance),
    };
    return status_code::success;
}

} // namespace cbor::tags::detail
