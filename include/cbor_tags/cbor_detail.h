#pragma once

#include "cbor_tags/cbor_concepts.h"

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <cstring>
#if defined(_MSC_VER)
#    include <intrin.h>
#endif
#include <ranges>
#include <span>
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

template <typename UInt>
[[nodiscard]] constexpr UInt portable_byteswap(UInt value) noexcept {
    static_assert(std::is_unsigned_v<UInt>, "portable_byteswap requires an unsigned type");
    UInt result = 0;
    for (std::size_t i = 0; i < sizeof(UInt); ++i) {
        result <<= 8;
        result |= static_cast<UInt>((value >> (i * 8)) & static_cast<UInt>(0xFF));
    }
    return result;
}

template <typename UInt>
[[nodiscard]] constexpr UInt intrinsic_byteswap(UInt value) noexcept {
    static_assert(std::is_unsigned_v<UInt>, "intrinsic_byteswap requires an unsigned type");
#if defined(__clang__) || defined(__GNUC__)
    if constexpr (sizeof(UInt) == 2) {
        return static_cast<UInt>(__builtin_bswap16(static_cast<unsigned short>(value)));
    } else if constexpr (sizeof(UInt) == 4) {
        return static_cast<UInt>(__builtin_bswap32(static_cast<unsigned int>(value)));
    } else if constexpr (sizeof(UInt) == 8) {
        return static_cast<UInt>(__builtin_bswap64(static_cast<unsigned long long>(value)));
    }
#elif defined(_MSC_VER)
    if constexpr (sizeof(UInt) == 2) {
        return static_cast<UInt>(_byteswap_ushort(static_cast<unsigned short>(value)));
    } else if constexpr (sizeof(UInt) == 4) {
        return static_cast<UInt>(_byteswap_ulong(static_cast<unsigned long>(value)));
    } else if constexpr (sizeof(UInt) == 8) {
        return static_cast<UInt>(_byteswap_uint64(static_cast<unsigned long long>(value)));
    }
#endif
    return portable_byteswap(value);
}

template <typename UInt>
[[nodiscard]] constexpr UInt big_to_native(UInt value) noexcept {
    static_assert(std::is_unsigned_v<UInt>, "big_to_native requires an unsigned type");
    if constexpr (std::endian::native == std::endian::big) {
        return value;
    } else {
        return intrinsic_byteswap(value);
    }
}

template <typename UInt>
[[nodiscard]] constexpr UInt native_to_big(UInt value) noexcept {
    static_assert(std::is_unsigned_v<UInt>, "native_to_big requires an unsigned type");
    if constexpr (std::endian::native == std::endian::big) {
        return value;
    } else {
        return intrinsic_byteswap(value);
    }
}

template <typename UInt>
[[nodiscard]] constexpr std::array<std::byte, sizeof(UInt)> make_big_endian_bytes(UInt value) noexcept {
    static_assert(std::is_unsigned_v<UInt>, "make_big_endian_bytes requires an unsigned type");
    auto big = native_to_big(value);
    std::array<std::byte, sizeof(UInt)> bytes{};
    std::memcpy(bytes.data(), &big, sizeof(UInt));
    return bytes;
}

template <typename Appender, typename Buffer, typename UInt>
constexpr void append_big_endian(Appender &appender, Buffer &buffer, UInt value) {
    static_assert(std::is_unsigned_v<UInt>, "append_big_endian requires an unsigned type");
    auto bytes = make_big_endian_bytes(value);
    appender(buffer, std::span<const std::byte>(bytes));
}

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
