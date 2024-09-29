#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>
#include <variant>

namespace cbor::tags {

struct array_view {
    std::span<const std::byte> data;
};

struct map_view {
    std::span<const std::byte> data;
};

struct tag_view {
    std::uint64_t              tag;
    std::span<const std::byte> data;
};

//
template <typename T>
concept tagged_type = requires(T) {
    { T::cbor_tag } -> std::convertible_to<std::uint64_t>;
};

template <typename T> using tag_pair = std::pair<std::uint64_t, T>;
template <typename T> auto make_tag(std::uint64_t tag, T &&value) { return tag_pair<T>{tag, std::forward<T>(value)}; }

using value = std::variant<std::uint64_t, std::int64_t, std::span<const std::byte>, std::string_view, array_view, map_view, tag_view, float,
                           double, bool, std::nullptr_t>;

// Comparison operators
template <typename T, typename U> constexpr std::strong_ordering lexicographic_compare(const T &lhs, const U &rhs) {
    if constexpr (std::is_same_v<T, std::string_view> && std::is_same_v<U, std::string_view>) {
        return lhs.compare(rhs) <=> 0;
    } else if constexpr (std::is_same_v<T, std::span<const std::byte>> && std::is_same_v<U, std::span<const std::byte>>) {
        return std::lexicographical_compare_three_way(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
    } else {
        const auto *lhs_bytes = reinterpret_cast<const std::byte *>(&lhs);
        const auto *rhs_bytes = reinterpret_cast<const std::byte *>(&rhs);
        return std::lexicographical_compare_three_way(lhs_bytes, lhs_bytes + sizeof(T), rhs_bytes, rhs_bytes + sizeof(U));
    }
}

constexpr auto operator<=>(const value &lhs, const value &rhs) {
    if (lhs.index() != rhs.index()) {
        return lhs.index() <=> rhs.index();
    }

    return std::visit(
        [](const auto &l, const auto &r) -> std::strong_ordering {
            using L = std::decay_t<decltype(l)>;
            using R = std::decay_t<decltype(r)>;

            if constexpr (std::is_same_v<L, R>) {
                if constexpr (std::is_same_v<L, std::nullptr_t>) {
                    return std::strong_ordering::equal;
                } else if constexpr (std::is_same_v<L, std::span<const std::byte>>) {
                    return lexicographic_compare(l, r);
                } else if constexpr (std::is_same_v<L, std::string_view>) {
                    return lexicographic_compare(l, r);
                } else if constexpr (std::is_same_v<L, array_view> || std::is_same_v<L, map_view> || std::is_same_v<L, tag_view>) {
                    return l <=> r;
                } else if constexpr (std::is_same_v<L, bool>) {
                    return l <=> r;
                } else if constexpr (std::is_same_v<L, float> || std::is_same_v<L, double>) {
                    if (l < r)
                        return std::strong_ordering::less;
                    if (l > r)
                        return std::strong_ordering::greater;
                    return std::strong_ordering::equal;
                } else if constexpr (std::is_arithmetic_v<L>) {
                    return l <=> r;
                } else {
                    // This should never happen
                    // std::unreachable(); // C++23
                    return std::strong_ordering::equal;
                }
            } else {
                // This should never happen due to the index check
                // std::unreachable(); // C++23
                return std::strong_ordering::equal;
            }
        },
        lhs, rhs);
}
// Equality operator
constexpr bool operator==(const value &lhs, const value &rhs) { return (lhs <=> rhs) == 0; }

enum class major_type : std::uint8_t {
    UnsignedInteger = 0,
    NegativeInteger = 1,
    ByteString      = 2,
    TextString      = 3,
    Array           = 4,
    Map             = 5,
    Tag             = 6,
    SimpleOrFloat   = 7
};

template <typename T> struct always_false : std::false_type {};

} // namespace cbor::tags

namespace std {
template <> struct hash<cbor::tags::value> {
    size_t operator()(const cbor::tags::value &v) const {
        return std::visit(
            [](const auto &x) -> size_t {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, std::uint64_t> || std::is_same_v<T, std::int64_t> || std::is_same_v<T, float> ||
                              std::is_same_v<T, double>) {
                    return std::hash<T>{}(x);
                } else if constexpr (std::is_same_v<T, std::span<const std::byte>>) {
                    return std::hash<std::string_view>{}(std::string_view(reinterpret_cast<const char *>(x.data()), x.size()));
                } else if constexpr (std::is_same_v<T, std::string_view>) {
                    return std::hash<std::string_view>{}(x);
                } else if constexpr (std::is_same_v<T, cbor::tags::array_view> || std::is_same_v<T, cbor::tags::map_view>) {
                    return std::hash<std::string_view>{}(std::string_view(reinterpret_cast<const char *>(x.data.data()), x.data.size()));
                } else if constexpr (std::is_same_v<T, cbor::tags::tag_view>) {
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