#pragma once

#include <concepts>
#include <ranges>

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
} // namespace cbor::tags