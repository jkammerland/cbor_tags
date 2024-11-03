#pragma once

#include "cbor.h"

namespace std {

template <> struct hash<cbor::tags::variant_contiguous> {
    size_t operator()(const cbor::tags::variant_contiguous &v) const noexcept {
        return std::visit(
            [](const auto &x) -> size_t {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, std::uint64_t> || std::is_same_v<T, std::int64_t> ||
                              std::is_same_v<T, cbor::tags::float16_t> || std::is_same_v<T, float> || std::is_same_v<T, double>) {
                    return std::hash<T>{}(x);
                } else if constexpr (std::is_same_v<T, std::span<const std::byte>>) {
                    return std::hash<std::string_view>{}(std::string_view(reinterpret_cast<const char *>(x.data()), x.size()));
                } else if constexpr (std::is_same_v<T, std::string_view>) {
                    return std::hash<std::string_view>{}(x);
                } else if constexpr (std::is_same_v<T, cbor::tags::binary_array_view> || std::is_same_v<T, cbor::tags::binary_map_view>) {
                    return std::hash<std::string_view>{}(std::string_view(reinterpret_cast<const char *>(x.data.data()), x.data.size()));
                } else if constexpr (std::is_same_v<T, cbor::tags::binary_tag_view>) {
                    return std::hash<std::uint64_t>{}(x.tag) ^
                           std::hash<std::string_view>{}(std::string_view(reinterpret_cast<const char *>(x.data.data()), x.data.size()));
                } else if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, std::nullptr_t>) {
                    return std::hash<T>{}(x);
                } else {
                    static_assert(cbor::tags::always_false<T>::value, "Non-exhaustive visitor!");
                }
            },
            v);
    }
};
} // namespace std