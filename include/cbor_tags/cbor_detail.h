#pragma once

#include "cbor_tags/cbor_concepts.h"

#include <cstring>
#include <type_traits>

namespace cbor::tags::detail {

template <typename T>
concept IsArrayConcept = requires {
    typename T::value_type;
    typename T::size_type;
    typename std::tuple_size<T>::type;
    requires std::is_same_v<T, std::array<typename T::value_type, std::tuple_size<T>::value>> ||
                 std::is_same_v<T, std::span<typename T::value_type>>;
};

template <typename T, bool IsArray = IsArrayConcept<T>>
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

template <typename T, bool IsContiguous = IsContiguous<T>>
    requires ValidCborBuffer<T>
struct reader;

template <typename T> struct reader<T, true> {
    using size_type  = T::size_type;
    using value_type = T::value_type;
    using iterator   = typename T::iterator;
    const T  &container_;
    size_type position_{};

    constexpr reader(const T &container) : container_(container) {}

    constexpr bool       empty() const noexcept { return position_ >= container_.size(); }
    constexpr value_type read() noexcept { return static_cast<value_type>(container_[position_++]); }
    constexpr value_type read(size_type offset) noexcept { return static_cast<value_type>(container_[position_ + offset]); }
};

template <typename T> struct reader<T, false> {
    using size_type  = T::size_type;
    using value_type = T::value_type;
    using iterator   = typename T::const_iterator;
    iterator position_;
    constexpr reader(const T &container) : position_(container.begin()) {}

    // Does not have random access so need to use iterator
    constexpr bool       empty(const T &container) const noexcept { return position_ == container.end(); }
    constexpr value_type read() noexcept { return static_cast<value_type>(*position_++); }
};

} // namespace cbor::tags::detail