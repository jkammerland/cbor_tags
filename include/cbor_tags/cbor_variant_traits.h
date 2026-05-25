#pragma once

#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

namespace cbor::tags {

// Customize this trait for variant-like types that cannot be detected structurally.
// A usable specialization must provide:
// - static constexpr std::size_t size
// - template <std::size_t I> using alternative = ...
// - static std::size_t index(const T&)
// - static get<I>(T&&), visit(visitor, variants...), and assign<I>(T&, value)
template <typename T> struct variant_traits;

namespace detail {

template <typename T> struct variant_dependent_false : std::false_type {};

struct variant_probe_visitor {
    template <typename... Args> constexpr void operator()(Args &&...) const noexcept {}
};

template <typename T>
concept VariantTemplateIndexable = requires(const T &value) {
    { value.index() } -> std::convertible_to<std::size_t>;
} || requires(const T &value) {
    { value.which() } -> std::convertible_to<int>;
};

template <typename Visitor, typename... Variants>
concept HasVariantVisit =
    requires(Visitor &&visitor, Variants &&...variants) { visit(std::forward<Visitor>(visitor), std::forward<Variants>(variants)...); };

template <typename Visitor, typename... Variants>
concept HasVariantApplyVisitor = requires(Visitor &&visitor, Variants &&...variants) {
    apply_visitor(std::forward<Visitor>(visitor), std::forward<Variants>(variants)...);
};

template <typename Variant>
concept VariantTemplateVisitable =
    HasVariantVisit<variant_probe_visitor, const Variant &> || HasVariantApplyVisitor<variant_probe_visitor, const Variant &>;

template <std::size_t I, typename Variant>
concept VariantIndexedGettable = requires(Variant &&value) { get<I>(std::forward<Variant>(value)); };

template <typename Alternative, typename Variant>
concept VariantTypedGettable = requires(Variant &&value) { get<Alternative>(std::forward<Variant>(value)); };

template <std::size_t I, typename Alternative, typename Variant> consteval bool variant_alternative_gettable() {
    if constexpr (VariantIndexedGettable<I, Variant>) {
        return true;
    } else {
        return VariantTypedGettable<Alternative, Variant>;
    }
}

template <typename Variant, typename... Ts, std::size_t... Is> consteval bool variant_template_gettable_impl(std::index_sequence<Is...>) {
    return (variant_alternative_gettable<Is, Ts, Variant &>() && ...) && (variant_alternative_gettable<Is, Ts, const Variant &>() && ...);
}

template <typename Variant, typename... Ts>
concept VariantTemplateGettable = (sizeof...(Ts) > 0U) && variant_template_gettable_impl<Variant, Ts...>(std::index_sequence_for<Ts...>{});

template <std::size_t I, typename Alternative, typename Variant> consteval bool variant_alternative_assignable() {
    if constexpr (requires(Variant &value, Alternative alternative) { value.template emplace<I>(std::move(alternative)); }) {
        return true;
    } else {
        return std::assignable_from<Variant &, Alternative>;
    }
}

template <typename Variant, typename... Ts, std::size_t... Is> consteval bool variant_template_assignable_impl(std::index_sequence<Is...>) {
    return (variant_alternative_assignable<Is, Ts, Variant>() && ...);
}

template <typename Variant, typename... Ts>
concept VariantTemplateAssignable =
    (sizeof...(Ts) > 0U) && variant_template_assignable_impl<Variant, Ts...>(std::index_sequence_for<Ts...>{});

template <typename Variant, typename... Ts>
concept VariantTemplateLike = VariantTemplateIndexable<Variant> && VariantTemplateVisitable<Variant> &&
                              VariantTemplateGettable<Variant, Ts...> && VariantTemplateAssignable<Variant, Ts...>;

template <typename Visitor, typename... Variants> constexpr decltype(auto) variant_adl_visit(Visitor &&visitor, Variants &&...variants) {
    using std::visit;
    if constexpr (requires { visit(std::forward<Visitor>(visitor), std::forward<Variants>(variants)...); }) {
        return visit(std::forward<Visitor>(visitor), std::forward<Variants>(variants)...);
    } else if constexpr (requires { apply_visitor(std::forward<Visitor>(visitor), std::forward<Variants>(variants)...); }) {
        return apply_visitor(std::forward<Visitor>(visitor), std::forward<Variants>(variants)...);
    } else {
        static_assert(variant_dependent_false<std::remove_cvref_t<Visitor>>::value,
                      "variant-like types require visit(visitor, variant...) or apply_visitor(visitor, variant...)");
    }
}

template <std::size_t I, typename Variant> constexpr decltype(auto) variant_adl_get(Variant &&value) {
    using std::get;
    if constexpr (requires { get<I>(std::forward<Variant>(value)); }) {
        return get<I>(std::forward<Variant>(value));
    } else {
        using variant_type     = std::remove_cvref_t<Variant>;
        using alternative_type = typename variant_traits<variant_type>::template alternative<I>;
        return get<alternative_type>(std::forward<Variant>(value));
    }
}

} // namespace detail

template <template <typename...> typename Variant, typename... Ts>
    requires detail::VariantTemplateLike<Variant<Ts...>, Ts...>
struct variant_traits<Variant<Ts...>> {
    using variant_type = Variant<Ts...>;

    static constexpr std::size_t size = sizeof...(Ts);

    template <std::size_t I> using alternative = std::tuple_element_t<I, std::tuple<Ts...>>;

    [[nodiscard]] static constexpr std::size_t index(const variant_type &value) noexcept {
        if constexpr (requires { value.index(); }) {
            return static_cast<std::size_t>(value.index());
        } else {
            return static_cast<std::size_t>(value.which());
        }
    }

    template <std::size_t I, typename VariantRef> static constexpr decltype(auto) get(VariantRef &&value) {
        return detail::variant_adl_get<I>(std::forward<VariantRef>(value));
    }

    template <typename Visitor, typename... VariantRefs> static constexpr decltype(auto) visit(Visitor &&visitor, VariantRefs &&...values) {
        return detail::variant_adl_visit(std::forward<Visitor>(visitor), std::forward<VariantRefs>(values)...);
    }

    template <std::size_t I, typename U> static constexpr void assign(variant_type &value, U &&decoded_value) {
        if constexpr (requires { value.template emplace<I>(std::forward<U>(decoded_value)); }) {
            value.template emplace<I>(std::forward<U>(decoded_value));
        } else if constexpr (std::assignable_from<variant_type &, U>) {
            value = std::forward<U>(decoded_value);
        } else {
            static_assert(detail::variant_dependent_false<U>::value,
                          "variant-like decode requires emplace<I>(value) or assignment from the decoded alternative");
        }
    }
};

namespace detail {

template <typename T, typename = void> struct is_variant_like : std::false_type {};

template <typename T>
struct is_variant_like<T, std::void_t<decltype(variant_traits<std::remove_cvref_t<T>>::size)>>
    : std::bool_constant<(variant_traits<std::remove_cvref_t<T>>::size > 0U)> {};

template <typename T>
concept VariantLike = is_variant_like<std::remove_cvref_t<T>>::value;

template <typename Variant> inline constexpr std::size_t variant_size_v = variant_traits<std::remove_cvref_t<Variant>>::size;

template <std::size_t I, typename Variant>
using variant_alternative_t = typename variant_traits<std::remove_cvref_t<Variant>>::template alternative<I>;

template <typename Variant> [[nodiscard]] constexpr std::size_t variant_index(const Variant &value) noexcept {
    return variant_traits<std::remove_cvref_t<Variant>>::index(value);
}

template <std::size_t I, typename Variant> constexpr decltype(auto) variant_get(Variant &&value) {
    return variant_traits<std::remove_cvref_t<Variant>>::template get<I>(std::forward<Variant>(value));
}

template <typename Visitor, typename Variant, typename... Variants>
constexpr decltype(auto) variant_visit(Visitor &&visitor, Variant &&value, Variants &&...values) {
    return variant_traits<std::remove_cvref_t<Variant>>::visit(std::forward<Visitor>(visitor), std::forward<Variant>(value),
                                                               std::forward<Variants>(values)...);
}

template <std::size_t I, typename Variant, typename U> constexpr void variant_assign(Variant &value, U &&decoded_value) {
    variant_traits<std::remove_cvref_t<Variant>>::template assign<I>(value, std::forward<U>(decoded_value));
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
