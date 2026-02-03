#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace someip::types {

template <int LenBits = 32, int AlignAfterBits = 0> struct utf8_string {
    using someip_string_tag = void;
    std::string value{};
};

template <int LenBits = 32, int AlignAfterBits = 0> struct utf16_string {
    using someip_string_tag = void;
    std::u16string value{};
};

} // namespace someip::types

