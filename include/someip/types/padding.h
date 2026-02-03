#pragma once

#include <cstddef>

namespace someip::types {

template <std::size_t N> struct pad_bytes {
    using someip_padding_tag = void;
    static constexpr std::size_t size_bytes = N;
};

template <std::size_t AlignBits> struct pad_to {
    using someip_padding_tag = void;
    static_assert(AlignBits % 8u == 0u, "AlignBits must be a multiple of 8");
    static_assert(AlignBits > 0u, "AlignBits must be > 0");
    static constexpr std::size_t align_bits  = AlignBits;
    static constexpr std::size_t align_bytes = AlignBits / 8u;
};

} // namespace someip::types

