#pragma once

#include <atomic>
#include <concepts>
#include <cstdint>
#include <ranges>
#include <type_traits>
#include <utility>

namespace cbor::tags {
template <typename T>
concept ValidCborBuffer = requires(T) {
    std::is_convertible_v<typename T::value_type, std::byte>;
    std::is_convertible_v<typename T::size_type, std::size_t>;
    requires std::input_or_output_iterator<typename T::iterator>;
};

template <typename T>
concept IsContiguous = requires(T) { requires std::ranges::contiguous_range<T>; };

template <typename Buffer>
    requires ValidCborBuffer<Buffer>
struct CborStream {
    Buffer           &buffer;
    Buffer::size_type head{};

    constexpr explicit CborStream(Buffer &buffer) : buffer(buffer) {}
    template <typename... Args> void operator()(const Args &...) { /* (de)serialize cbor onto buffer */ }
};

template <typename T, typename... Args>
concept IsBracesContructible = requires { T{std::declval<Args>()...}; };

// template <typename T, typename... Args>
// concept IsBracesContructible = requires(Args... args) { T{args...}; };

template <typename T>
concept IsTuple = requires {
    typename std::tuple_size<T>::type;
    typename std::tuple_element<0, T>::type;
};

template <typename T>
concept IsAggregateOrTuple = std::is_aggregate_v<T> || IsTuple<T>;

template <typename T>
concept IsNonAggregate = !IsAggregateOrTuple<T>;

struct any {
    template <class T> constexpr operator T() const {
        if constexpr (is_optional_v<T>) {
            return T{typename T::value_type{}};
        } else {
            return T{};
        }
    }

  private:
    template <class T> static constexpr bool is_optional_v                   = false;
    template <class T> static constexpr bool is_optional_v<std::optional<T>> = true;
};

template <std::uint64_t N> struct tag {
    static constexpr std::uint64_t cbor_tag = N;
    operator std::uint64_t() { return tag::cbor_tag; }
};

template <typename T>
concept HasCborTag = requires {
    { T::cbor_tag } -> std::convertible_to<std::uint64_t>;
};

template <typename T>
concept IsTaggedPair = requires(T t) {
    requires std::is_constructible_v<T, tag<5>, typename T::second_type>;
    { std::get<0>(t) } -> std::convertible_to<std::uint64_t>;
    typename T::second_type;
};

template <typename T>
concept TaggedCborType = HasCborTag<T> || IsTaggedPair<T>;

namespace detail {

// Helper for decimal parsing
template <char... Chars> constexpr std::uint64_t parse_decimal() {
    std::uint64_t result = 0;
    ((result = result * 10 + (Chars - '0')), ...);
    return result;
}

// Helper for hexadecimal parsing
template <char C> constexpr std::uint64_t hex_to_int() {
    if (C >= '0' && C <= '9')
        return C - '0';
    if (C >= 'a' && C <= 'f')
        return C - 'a' + 10;
    if (C >= 'A' && C <= 'F')
        return C - 'A' + 10;
    return 0; // Invalid hex character
}

template <char... Chars> constexpr std::uint64_t parse_hex() {
    std::uint64_t result = 0;
    ((result = (result << 4) | hex_to_int<Chars>()), ...);
    return result;
}
} // namespace detail

namespace literals {

// Decimal tag literal
template <char... Chars> constexpr auto operator"" _tag() { return tag<detail::parse_decimal<Chars...>()>{}; }

// Hexadecimal tag literal
template <char... Chars> constexpr auto operator"" _hex_tag() { return tag<detail::parse_hex<Chars...>()>{}; }

} // namespace literals

} // namespace cbor::tags