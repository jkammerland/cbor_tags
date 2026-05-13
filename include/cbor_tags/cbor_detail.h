#pragma once

#include "cbor_tags/cbor_concepts.h"
#include "cbor_tags/cbor_reflection_config.h"
#include "cbor_tags/cbor_reflection_count.h"

#include <concepts>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace cbor::tags::detail {

template <typename T>
concept AppendableContainer = requires {
    typename T::value_type;
    typename T::size_type;
};

template <typename T, bool IsArray = IsFixedArray<T>>
    requires AppendableContainer<T>
struct appender;

template <typename Value, typename Container> constexpr bool uses_container_allocator_for() {
    if constexpr (requires(Container &container) { container.get_allocator(); }) {
        using allocator_type = decltype(std::declval<Container &>().get_allocator());
        return std::uses_allocator_v<Value, allocator_type>;
    } else {
        return false;
    }
}

template <typename Value, typename Container> constexpr Value make_decode_value_for(Container &container) {
    if constexpr (uses_container_allocator_for<Value, Container>()) {
        return std::make_from_tuple<Value>(std::uses_allocator_construction_args<Value>(container.get_allocator()));
    } else {
        return Value{};
    }
}

template <typename Container, typename Pair>
concept AssignableInsertOrAssignMap = IsMap<Container> && IsPairLike<Pair> && requires(Container &container, Pair &&value) {
    requires std::assignable_from<typename Container::mapped_type &, decltype(pair_second(std::forward<Pair>(value)))>;
    container.insert_or_assign(pair_first(std::forward<Pair>(value)), pair_second(std::forward<Pair>(value)));
};

template <typename T> struct appender<T, false> {
    using value_type = T::value_type;

    constexpr void operator()(T &container, const value_type &value)
        requires(!IsMap<T>)
    {
        container.push_back(value);
    }

    template <typename Pair>
        requires IsMap<T> && IsPairLike<Pair>
    constexpr void operator()(T &container, Pair &&value) {
        if constexpr (AssignableInsertOrAssignMap<T, Pair>) {
            container.insert_or_assign(pair_first(std::forward<Pair>(value)), pair_second(std::forward<Pair>(value)));
        } else {
            container.insert(std::forward<Pair>(value));
        }
    }

    template <typename... Ts> constexpr void multi_append(T &container, Ts &&...values) {
        static_assert(sizeof...(Ts) > 1, "multi_append requires at least 2 arguments, use operator() for single values");
        constexpr bool all_1_byte = ((sizeof(Ts) == 1) && ...);
        static_assert(all_1_byte, "multi_append requires all arguments to be 1 byte types");

        container.insert(container.end(), {std::forward<Ts>(values)...});
    }

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
    size_type head_{};

    constexpr void ensure_capacity(const T &container, size_type additional) const {
        if (additional == 0) {
            return;
        }
        if (head_ > container.size() || (container.size() - head_) < additional) {
            throw std::runtime_error("CBOR output buffer too small");
        }
    }

    template <typename... Ts> constexpr void multi_append(T &container, Ts &&...values) {
        static_assert(sizeof...(Ts) > 1, "multi_append requires at least 2 arguments, use operator() for single values");
        constexpr bool all_1_byte = ((sizeof(Ts) == 1) && ...);
        static_assert(all_1_byte, "multi_append requires all arguments to be 1 byte types");
        ensure_capacity(container, static_cast<size_type>(sizeof...(Ts)));
        ((container[head_++] = std::forward<Ts>(values)), ...);
    }

    constexpr void operator()(T &container, value_type value) {
        ensure_capacity(container, 1);
        container[head_++] = value;
    }
    constexpr void operator()(T &container, std::span<const std::byte> values) {
        ensure_capacity(container, static_cast<size_type>(values.size()));
        std::memcpy(container.data() + head_, reinterpret_cast<const value_type *>(values.data()), values.size());
        head_ += values.size();
    }
    constexpr void operator()(T &container, std::string_view value) {
        ensure_capacity(container, static_cast<size_type>(value.size()));
        std::memcpy(container.data() + head_, reinterpret_cast<const value_type *>(value.data()), value.size());
        head_ += value.size();
    }
};

template <typename OutputBuffer, typename R>
concept DirectlyInsertableByteRange =
    (!IsFixedArray<std::remove_cvref_t<OutputBuffer>>) && std::ranges::common_range<R> &&
    std::same_as<std::remove_cvref_t<std::ranges::range_reference_t<R>>, typename std::remove_cvref_t<OutputBuffer>::value_type> &&
    requires(OutputBuffer &output, R &&range) { output.insert(output.end(), std::ranges::begin(range), std::ranges::end(range)); };

template <std::ranges::contiguous_range R>
    requires std::ranges::sized_range<R> && ByteLikeRange<R>
[[nodiscard]] constexpr std::span<const std::byte> as_byte_span(R &&range) {
    auto values = std::span{std::ranges::data(range), static_cast<std::size_t>(std::ranges::size(range))};
    return std::as_bytes(values);
}

template <typename Appender, typename OutputBuffer, typename R>
    requires ByteLikeRange<R>
constexpr void append_byte_range(Appender &appender, OutputBuffer &output, R &&range) {
    using output_byte = typename std::remove_cvref_t<OutputBuffer>::value_type;

    if constexpr (requires {
                      { range.span() } -> std::same_as<std::span<const std::byte>>;
                  }) {
        appender(output, range.span());
    } else if constexpr (std::ranges::contiguous_range<R> && std::ranges::sized_range<R>) {
        appender(output, as_byte_span(std::forward<R>(range)));
    } else if constexpr (DirectlyInsertableByteRange<OutputBuffer, R>) {
        output.insert(output.end(), std::ranges::begin(range), std::ranges::end(range));
    } else {
        for (auto &&byte : range) {
            appender(output, static_cast<output_byte>(byte));
        }
    }
}

template <typename T, bool IsContiguous = IsContiguous<T>>
    requires CborInputBuffer<T>
struct reader;

template <typename T, bool HasNestedSize = requires { typename std::remove_cvref_t<T>::size_type; },
          bool IsSizedRange = std::ranges::sized_range<const std::remove_cvref_t<T>>>
struct reader_size_type {
    using type = std::size_t;
};

template <typename T, bool IsSizedRange> struct reader_size_type<T, true, IsSizedRange> {
    using type = typename std::remove_cvref_t<T>::size_type;
};

template <typename T> struct reader_size_type<T, false, true> {
    using type = std::ranges::range_size_t<const std::remove_cvref_t<T>>;
};

[[nodiscard]] constexpr auto negative_seek_magnitude(std::ptrdiff_t value) noexcept {
    using magnitude_type = std::make_unsigned_t<std::ptrdiff_t>;
    if (value == std::numeric_limits<std::ptrdiff_t>::min()) {
        return static_cast<magnitude_type>(std::numeric_limits<std::ptrdiff_t>::max()) + magnitude_type{1};
    }
    return static_cast<magnitude_type>(-value);
}

template <typename T> struct reader<T, true> {
    using size_type  = typename reader_size_type<T>::type;
    using value_type = std::byte;
    using iterator   = std::ranges::iterator_t<const T>;
    size_type position_{0};

    constexpr reader(const T &) {}

    constexpr size_type size(const T &container) const noexcept {
        if constexpr (std::ranges::sized_range<const T>) {
            return static_cast<size_type>(std::ranges::size(container));
        } else {
            return static_cast<size_type>(std::ranges::distance(container));
        }
    }

    constexpr bool empty(const T &container) const noexcept { return position_ >= size(container); }
    constexpr bool empty(const T &container, size_type offset) const noexcept {
        const auto range_size = size(container);
        return position_ >= range_size || offset >= (range_size - position_);
    }
    constexpr value_type read(const T &container) noexcept {
        auto result = static_cast<value_type>(std::ranges::data(container)[position_]);
        ++position_;
        return result;
    }
    constexpr value_type read(const T &container, size_type offset) noexcept {
        return static_cast<value_type>(std::ranges::data(container)[position_ + offset]);
    }

    constexpr void seek(std::ptrdiff_t i) {
        if (i >= 0) {
            position_ += static_cast<size_type>(i);
        } else {
            const auto amount = negative_seek_magnitude(i);
            if (std::cmp_greater(amount, position_)) {
                throw std::runtime_error("CBOR input seek before begin");
            }
            position_ -= static_cast<size_type>(amount);
        }
    }
};

template <typename T> struct reader<T, false> {
    using size_type  = typename reader_size_type<T>::type;
    using value_type = std::byte;
    using iterator   = std::ranges::iterator_t<const T>;
    iterator  position_;
    size_type current_offset_{0};
    constexpr reader(const T &container) : position_(std::ranges::begin(container)) {}

    // Does not have random access so need to use iterator
    constexpr bool empty(const T &container) const noexcept { return position_ == std::ranges::end(container); }
    constexpr bool empty(const T &container, size_type offset) const noexcept {
        if constexpr (std::ranges::sized_range<const T>) {
            const auto range_size = static_cast<size_type>(std::ranges::size(container));
            return current_offset_ >= range_size || offset >= (range_size - current_offset_);
        }

        auto it = position_;
        for (size_type i = 0; i <= offset; ++i) {
            if (it == std::ranges::end(container)) {
                return true;
            }
            ++it;
        }
        return false;
    }
    constexpr value_type read(const T &) noexcept {
        auto result = static_cast<value_type>(*position_);
        ++position_;
        ++current_offset_;
        return result;
    }

    constexpr value_type read(const T &, size_type offset) noexcept {
        auto it = position_;
        std::advance(it, static_cast<std::ptrdiff_t>(offset));
        return static_cast<value_type>(*it);
    }

    constexpr void seek(std::ptrdiff_t i) {
        if (i < 0) {
            const auto amount = negative_seek_magnitude(i);
            if (std::cmp_greater(amount, current_offset_)) {
                throw std::runtime_error("CBOR input seek before begin");
            }
        }
        position_ = std::next(position_, i);
        if (i >= 0) {
            current_offset_ += static_cast<size_type>(i);
        } else {
            current_offset_ -= static_cast<size_type>(negative_seek_magnitude(i));
        }
    }
};

template <typename Tuple> constexpr auto tuple_tail(Tuple &&tuple) {
    return std::apply([](auto &&, auto &&...tail) { return std::forward_as_tuple(std::forward<decltype(tail)>(tail)...); },
                      std::forward<Tuple>(tuple));
}

template <typename T, typename ThisPtr> constexpr T &underlying(ThisPtr this_ptr) { return static_cast<T &>(*this_ptr); }

template <typename Encoder, typename C> inline constexpr auto adl_indirect_encode(Encoder &enc, const C &c) { return encode(enc, c); }
template <typename Decoder, typename C> inline constexpr auto adl_indirect_decode(Decoder &dec, C &&c) {
    return decode(dec, std::forward<C>(c));
}

template <typename T> inline constexpr auto get_major_6_tag_from_tuple(const T &t) {
    if constexpr (HasDynamicTag<T> || HasStaticTag<T>) {
        return std::get<0>(t);
    }
}

template <typename T> static constexpr auto get_major_6_tag_from_class(const T &t) {
    // static_assert(IsClassWithTagOverload<T>, "T must be a class with tag overload");

    if constexpr (HasTagMember<T>) {
        return Access::cbor_tag(t);
    } else if constexpr (HasTagNonConstructible<T>) {
        return cbor::tags::cbor_tag<T>();
    } else if constexpr (HasTagFreeFunction<T>) {
        return cbor_tag(t);
    } else {
        return -1;
        // return detail::FalseType{}; // This doesn't work so well across compilers
    }
}

template <typename T> static constexpr auto get_major_6_tag_from_class() {
    // static_assert(IsClassWithTagOverload<T>, "T must be a class with tag overload");
    if constexpr (HasTagMember<T>) {
        return Access::cbor_tag<T>();
    } else if constexpr (HasTagNonConstructible<T>) {
        return cbor::tags::cbor_tag<T>();
    } else if constexpr (HasTagFreeFunction<T>) {
        return cbor_tag(T{});
    } else {
        return -1;
        // return detail::FalseType{}; // This doesn't work so well across compilers
    }
}

template <typename T> static constexpr auto get_tag_from_any() {
    if constexpr (HasInlineTag<T> || is_static_tag_t<T>::value)
        return T::cbor_tag;
    else if constexpr (HasStaticTag<T> || HasDynamicTag<T>)
        return decltype(T::cbor_tag){};
    else if constexpr (IsTaggedTuple<T>) {
        using FirstTupleMemberType = std::remove_reference_t<decltype(std::get<0>(T{}))>;
        static_assert(is_static_tag_t<FirstTupleMemberType>::value || is_dynamic_tag_t<FirstTupleMemberType>,
                      "T must be a static or dynamic tag");
        return std::get<0>(T{}).cbor_tag;
    } else {
        return get_major_6_tag_from_class<T>();
    }
}

} // namespace cbor::tags::detail
