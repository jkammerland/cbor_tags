#pragma once

#include <variant>

namespace someip::types {

template <class Variant, int LenBits = 32, int SelectorBits = 32, int AlignPayloadBits = 0> struct union_variant {
    using someip_union_tag = void;
    Variant value{};
};

} // namespace someip::types

