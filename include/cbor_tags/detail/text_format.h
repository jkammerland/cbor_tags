#pragma once

#if __has_include("cbor_tags/cbor_tags_config.h")
#include "cbor_tags/cbor_tags_config.h"
#endif

#ifndef CBOR_TAGS_STL_ONLY
#define CBOR_TAGS_STL_ONLY 0
#endif

#include <string>
#include <utility>

#if CBOR_TAGS_STL_ONLY
#include <format>
#else
#include <fmt/base.h>
#include <fmt/format.h>
#endif

namespace cbor::tags::detail::text_format {

#if CBOR_TAGS_STL_ONLY

template <typename... Args> [[nodiscard]] std::string format(std::format_string<Args...> pattern, Args &&...args) {
    return std::format(pattern, std::forward<Args>(args)...);
}

template <typename OutputIt, typename... Args> auto format_to(OutputIt output, std::format_string<Args...> pattern, Args &&...args) {
    return std::format_to(output, pattern, std::forward<Args>(args)...);
}

#else

using fmt::format;
using fmt::format_to;

#endif

} // namespace cbor::tags::detail::text_format
