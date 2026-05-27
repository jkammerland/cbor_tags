#pragma once

#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace cbor::tags {

// Specialize this trait to opt non-std variant types into cbor_tags variant dispatch.
// A usable specialization must provide:
// - static constexpr std::size_t size
// - template <std::size_t I> using alternative = ...
// - static std::size_t index(const T&)
// - static get<I>(T&) and get<I>(const T&)
// - static visit(visitor, variant) and visit(visitor, variant, variant)
// - static assign<I>(T&, value)
// Recognition checks the type-level shape plus index(). The remaining hooks are
// instantiated by encoder, decoder, and operator call sites.
template <typename T> struct variant_traits;

template <typename... Ts> struct variant_traits<std::variant<Ts...>> {
    using variant_type = std::variant<Ts...>;

    static constexpr std::size_t size = sizeof...(Ts);

    template <std::size_t I> using alternative = std::tuple_element_t<I, std::tuple<Ts...>>;

    [[nodiscard]] static constexpr std::size_t index(const variant_type &value) noexcept { return value.index(); }

    template <std::size_t I, typename VariantRef> static constexpr decltype(auto) get(VariantRef &&value) {
        return std::get<I>(std::forward<VariantRef>(value));
    }

    template <typename Visitor, typename... VariantRefs> static constexpr decltype(auto) visit(Visitor &&visitor, VariantRefs &&...values) {
        return std::visit(std::forward<Visitor>(visitor), std::forward<VariantRefs>(values)...);
    }

    template <std::size_t I, typename U> static constexpr void assign(variant_type &value, U &&decoded_value) {
        value.template emplace<I>(std::forward<U>(decoded_value));
    }
};

namespace detail {

template <typename T> using variant_traits_t = variant_traits<std::remove_cvref_t<T>>;

template <typename T> struct is_std_variant : std::false_type {};

template <typename... Ts> struct is_std_variant<std::variant<Ts...>> : std::true_type {};

template <typename T, typename = void> struct variant_traits_size : std::integral_constant<std::size_t, 0U> {
    static constexpr bool valid = false;
};

template <typename T>
struct variant_traits_size<T, std::void_t<decltype(std::integral_constant<std::size_t, variant_traits_t<T>::size>{})>>
    : std::integral_constant<std::size_t, variant_traits_t<T>::size> {
    static constexpr bool valid = true;
};

template <typename T, std::size_t I> using variant_trait_alternative_probe_t = typename variant_traits_t<T>::template alternative<I>;

template <typename T, std::size_t I>
concept VariantTraitAlternative = requires { typename variant_trait_alternative_probe_t<T, I>; };

template <typename T>
concept VariantTraitIndexable = requires(const T &const_value) {
    { variant_traits_t<T>::index(const_value) } -> std::convertible_to<std::size_t>;
};

template <typename T, std::size_t... Is> consteval bool variant_traits_complete_impl(std::index_sequence<Is...>) {
    return VariantTraitIndexable<std::remove_cvref_t<T>> && (VariantTraitAlternative<std::remove_cvref_t<T>, Is> && ...);
}

template <typename T> consteval bool variant_traits_complete() {
    if constexpr (!variant_traits_size<T>::valid || variant_traits_size<T>::value == 0U) {
        return false;
    } else {
        return variant_traits_complete_impl<T>(std::make_index_sequence<variant_traits_size<T>::value>{});
    }
}

template <typename T, typename = void> struct is_variant_like : std::false_type {};

template <typename T> struct is_variant_like<T, std::enable_if_t<is_std_variant<std::remove_cvref_t<T>>::value>> : std::true_type {};

template <typename T>
struct is_variant_like<T, std::enable_if_t<!is_std_variant<std::remove_cvref_t<T>>::value && variant_traits_complete<T>()>>
    : std::true_type {};

template <typename T>
concept VariantLike = is_variant_like<std::remove_cvref_t<T>>::value;

template <typename Variant> inline constexpr std::size_t variant_size_v = variant_traits_t<Variant>::size;

template <std::size_t I, typename Variant> using variant_alternative_t = typename variant_traits_t<Variant>::template alternative<I>;

template <typename Variant> [[nodiscard]] constexpr std::size_t variant_index(const Variant &value) noexcept {
    return variant_traits_t<Variant>::index(value);
}

template <std::size_t I, typename Variant> constexpr decltype(auto) variant_get(Variant &&value) {
    return variant_traits_t<Variant>::template get<I>(std::forward<Variant>(value));
}

template <typename Visitor, typename Variant, typename... Variants>
constexpr decltype(auto) variant_visit(Visitor &&visitor, Variant &&value, Variants &&...values) {
    return variant_traits_t<Variant>::visit(std::forward<Visitor>(visitor), std::forward<Variant>(value),
                                            std::forward<Variants>(values)...);
}

template <std::size_t I, typename Variant, typename U> constexpr void variant_assign(Variant &value, U &&decoded_value) {
    variant_traits_t<Variant>::template assign<I>(value, std::forward<U>(decoded_value));
}

template <typename Variant, typename Fn, std::size_t... Is>
constexpr decltype(auto) with_variant_alternatives_impl(Fn &&fn, std::index_sequence<Is...>) {
    return std::forward<Fn>(fn).template operator()<variant_alternative_t<Is, Variant>...>();
}

template <typename Variant, typename Fn> constexpr decltype(auto) with_variant_alternatives(Fn &&fn) {
    return with_variant_alternatives_impl<std::remove_cvref_t<Variant>>(
        std::forward<Fn>(fn), std::make_index_sequence<variant_size_v<std::remove_cvref_t<Variant>>>{});
}

template <typename Variant, typename Fn, std::size_t... Is>
constexpr decltype(auto) with_variant_alternative_indices_impl(Fn &&fn, std::index_sequence<Is...>) {
    return std::forward<Fn>(fn).template operator()<Is...>();
}

template <typename Variant, typename Fn> constexpr decltype(auto) with_variant_alternative_indices(Fn &&fn) {
    return with_variant_alternative_indices_impl<std::remove_cvref_t<Variant>>(
        std::forward<Fn>(fn), std::make_index_sequence<variant_size_v<std::remove_cvref_t<Variant>>>{});
}

} // namespace detail

} // namespace cbor::tags
