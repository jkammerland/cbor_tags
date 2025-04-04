#pragma once

#include "cbor_tags/cbor_concepts.h"

#include <concepts>
#include <cstdio>
#include <cstring>
#include <ranges>
#include <stdexcept>
#include <type_traits>

namespace cbor::tags::detail {

template <typename T, bool IsArray = IsFixedArray<T>>
    requires ValidCborBuffer<T>
struct appender;

template <typename T> struct appender<T, false> {
    using value_type = T::value_type;

    constexpr void operator()(T &container, const value_type &value) {
        if constexpr (IsMap<T>) {
            const auto &[key, mapped_value] = value;

            if constexpr (IsMultiMap<T>) {
                container.insert({key, mapped_value});
            } else {
                container.insert_or_assign(key, mapped_value);
            }
        } else {
            container.push_back(value);
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

    template <typename... Ts> constexpr void multi_append(T &container, Ts &&...values) {
        static_assert(sizeof...(Ts) > 1, "multi_append requires at least 2 arguments, use operator() for single values");
        constexpr bool all_1_byte = ((sizeof(Ts) == 1) && ...);
        static_assert(all_1_byte, "multi_append requires all arguments to be 1 byte types");
        ((container[head_++] = std::forward<Ts>(values)), ...);
    }

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
    using value_type = std::byte;
    using iterator   = typename T::iterator;
    size_type position_;

    constexpr reader(const T &) : position_(0) {}

    constexpr bool       empty(const T &container) const noexcept { return position_ >= container.size(); }
    constexpr bool       empty(const T &container, size_type offset) const noexcept { return position_ + offset >= container.size(); }
    constexpr value_type read(const T &container) noexcept { return static_cast<value_type>(container[position_++]); }
    constexpr value_type read(const T &container, size_type offset) noexcept {
        return static_cast<value_type>(container[position_ + offset]);
    }

    constexpr void seek(int i) { position_ += i; }
};

template <typename T> struct reader<T, false> {
    using size_type  = T::size_type;
    using value_type = std::byte;
    using iterator   = typename T::const_iterator;
    iterator  position_;
    size_type current_offset_{0};
    constexpr reader(const T &container) : position_(container.cbegin()) {}

    // Does not have random access so need to use iterator
    constexpr bool empty(const T &container) const noexcept { return position_ == container.cend(); }
    constexpr bool empty(const T &container, size_type offset) const noexcept { return (current_offset_ + offset) >= container.size(); }
    constexpr value_type read(const T &) noexcept {
        auto result = static_cast<value_type>(*position_);
        ++position_;
        ++current_offset_;
        return result;
    }

    constexpr value_type read(const T &, size_type offset) noexcept {
        throw std::runtime_error("Not implemented");
        auto it = std::next(position_, offset);
        return static_cast<value_type>(*it);
    }

    constexpr void seek(int i) {
        position_ = std::next(position_, i);
        current_offset_ += i;
    }
};

template <typename Tuple> constexpr auto tuple_tail(Tuple &&tuple) {
    return std::apply([](auto &&, auto &&...tail) { return std::forward_as_tuple(std::forward<decltype(tail)>(tail)...); },
                      std::forward<Tuple>(tuple));
}

template <typename T, typename... TArgs>
    requires IsAggregate<T> || IsTuple<T>
constexpr std::size_t num_bindings_impl() {
    if constexpr (requires { T{std::declval<TArgs>()...}; }) {
        return num_bindings_impl<T, any, TArgs...>();
    } else {
        return sizeof...(TArgs) - 1;
    }
}

// template <IsTuple T> constexpr std::size_t num_bindings_impl() { return std::tuple_size_v<T>; }

template <typename T>
    requires IsAggregate<T> || IsTuple<T>
constexpr auto aggregate_binding_count = detail::num_bindings_impl<T, any>();

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
        return detail::FalseType{};
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
    if constexpr (HasInlineTag<T>)
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