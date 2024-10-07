#pragma once

#include "cbor.h"

namespace cbor::tags::detail {

template <typename T>
concept IsArrayConcept = requires {
    typename T::value_type;
    typename T::size_type;
    typename std::tuple_size<T>::type;
    requires std::is_same_v<T, std::array<typename T::value_type, std::tuple_size<T>::value>>;
};

template <typename T, bool IsArray>
    requires ValidCborBuffer<T>
struct appender;

template <typename T> struct appender<T, false> {
    using value_type = T::value_type;
    constexpr void operator()(T &container, value_type value) { container.push_back(value); }
    constexpr void operator()(T &container, std::span<const std::byte> values) {
        container.insert(container.end(), reinterpret_cast<const value_type *>(values.data()),
                         reinterpret_cast<const value_type *>(values.data() + values.size()));
    }
    constexpr void operator()(T &container, std::string_view value) {
        container.insert(container.end(), reinterpret_cast<const value_type *>(value.data()),
                         reinterpret_cast<const value_type *>(value.data() + value.size()));
    }
};

template <typename T> struct appender<T, true> {
    using size_type  = T::size_type;
    using value_type = T::value_type;
    size_type      head_{};
    constexpr void operator()(T &container, value_type value) { container[head_++] = value; }
    constexpr void operator()(T &container, std::span<const std::byte> values) {
        std::memcpy(container.data() + head_, reinterpret_cast<const value_type *>(values.data()), values.size());
        head_ += values.size();
    }
    constexpr void operator()(T &container, std::string_view value) {
        std::memcpy(container.data() + head_, reinterpret_cast<const value_type *>(value.data()), value.size());
        head_ += value.size();
    }
};

} // namespace cbor::tags::detail