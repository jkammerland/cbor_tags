#pragma once

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

template <typename T>
concept HasCborTag = requires {
    { T::cbor_tag } -> std::convertible_to<std::uint64_t>;
};

template <typename T>
concept IsTaggedPair = requires(T t) {
    requires std::is_constructible_v<T, std::uint64_t, typename T::second_type>;
    { std::get<0>(t) } -> std::convertible_to<std::uint64_t>;
    typename T::second_type;
};

template <typename T>
concept TaggedCborType = HasCborTag<T> || IsTaggedPair<T>;

template <typename T, typename... Args>
concept IsBracesContructible = requires { T{std::declval<Args>()...}; };

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
    template <class T> constexpr operator T(); // non explicit
};

} // namespace cbor::tags