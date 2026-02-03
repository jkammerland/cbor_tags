#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace someip::types {

template <class T, int LenBits = 32, int AlignAfterBits = 0> struct dyn_array {
    using someip_array_tag = void;
    std::vector<T> value{};
};

template <class T, std::size_t N, int OptionalLenBits = 0> struct fixed_array {
    using someip_array_tag = void;
    std::array<T, N> value{};
};

} // namespace someip::types

