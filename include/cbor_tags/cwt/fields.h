#pragma once

#include "cbor_tags/cbor_integer.h"

#include <concepts>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace cbor::tags::cwt {
namespace detail {

template <std::size_t N> struct field_text {
    char characters[N];
    constexpr field_text(const char (&text)[N]) {
        for (std::size_t i = 0; i < N; ++i) {
            characters[i] = text[i];
        }
    }
    constexpr std::string_view view() const { return {characters, N - 1}; }
};

template <auto Label, typename T> struct field_binding {
    T *value;

    static constexpr auto label_value() {
        if constexpr (std::integral<decltype(Label)>) {
            if constexpr (std::signed_integral<decltype(Label)>) {
                if constexpr (Label < 0) {
                    return integer{std::uint64_t(-(Label + 1)) + 1, true};
                }
            }
            return integer{std::uint64_t(Label)};
        } else {
            return Label.view();
        }
    }
};

template <typename T> inline constexpr bool field_optional                   = false;
template <typename T> inline constexpr bool field_optional<std::optional<T>> = true;

template <typename A, typename B> consteval bool same_field_label() {
    if constexpr (std::same_as<decltype(A::label_value()), decltype(B::label_value())>) {
        return A::label_value() == B::label_value();
    } else {
        return false;
    }
}

template <typename... Bindings> struct unique_field_labels : std::true_type {};
template <typename First, typename... Rest>
struct unique_field_labels<First, Rest...>
    : std::bool_constant<((!same_field_label<First, Rest>()) && ...) && unique_field_labels<Rest...>::value> {};

template <typename... Bindings> struct field_map {
    static_assert(unique_field_labels<Bindings...>::value, "CWT fields require unique labels");
    std::tuple<Bindings...> bindings;
};

template <typename Encoder, typename... Bindings>
auto encode(Encoder &, const field_map<Bindings...> &) -> typename Encoder::expected_type {
    static_assert(sizeof...(Bindings) == static_cast<std::size_t>(-1), "CWT fields require codec::cwt");
    return {};
}

template <typename Decoder, typename... Bindings> auto decode(Decoder &, field_map<Bindings...> &&) -> typename Decoder::expected_type {
    static_assert(sizeof...(Bindings) == static_cast<std::size_t>(-1), "CWT fields require codec::cwt");
    return {};
}

template <auto Label, typename T> constexpr auto field_tuple(field_binding<Label, T> value) { return std::tuple{value}; }
template <typename... Bindings> constexpr auto   field_tuple(field_map<Bindings...> value) { return value.bindings; }

template <typename Map, typename Function> constexpr void for_each_field(Map &map, Function &&function) {
    std::apply([&](auto &...binding) { (function(binding), ...); }, map.bindings);
}

} // namespace detail

// Bind named storage; descriptors may be temporary, their referenced values may not.
template <auto Label, typename T>
    requires(std::integral<decltype(Label)> && !std::same_as<decltype(Label), bool>)
constexpr auto field(T &value) {
    return detail::field_binding<Label, T>{&value};
}

template <detail::field_text Label, typename T> constexpr auto field(T &value) { return detail::field_binding<Label, T>{&value}; }

template <typename... Bindings> constexpr auto fields(Bindings... bindings) {
    return std::apply([](auto... binding) { return detail::field_map<decltype(binding)...>{{binding...}}; },
                      std::tuple_cat(detail::field_tuple(bindings)...));
}

} // namespace cbor::tags::cwt
