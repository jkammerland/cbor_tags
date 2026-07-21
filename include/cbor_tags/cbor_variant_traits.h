#pragma once

#include <cstddef>
#include <tuple>
#include <utility>
#include <variant>

namespace cbor::tags {

// Specialize this trait to opt non-std variant types into cbor_tags variant dispatch.
// A usable specialization must provide:
// - static constexpr std::size_t size
// - template <std::size_t I> using alternative = ...
// - static std::size_t index(const T&) noexcept
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

} // namespace cbor::tags
