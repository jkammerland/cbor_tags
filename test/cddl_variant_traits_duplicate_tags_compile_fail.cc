#include "cbor_tags/extensions/cbor_visualization.h"
#include "cbor_tags/extensions/rfc8746_typed_arrays.h"

#include <cstddef>
#include <cstdint>
#include <fmt/format.h>
#include <tuple>
#include <utility>
#include <variant>

namespace variant_traits_compile_fail_test {

template <typename... Ts> struct manual_variant {
    std::variant<Ts...> storage;
};

} // namespace variant_traits_compile_fail_test

namespace cbor::tags {

template <typename... Ts> struct variant_traits<variant_traits_compile_fail_test::manual_variant<Ts...>> {
    using variant_type = variant_traits_compile_fail_test::manual_variant<Ts...>;

    static constexpr std::size_t size = sizeof...(Ts);

    template <std::size_t I> using alternative = std::tuple_element_t<I, std::tuple<Ts...>>;

    [[nodiscard]] static constexpr std::size_t index(const variant_type &value) noexcept { return value.storage.index(); }

    template <std::size_t I, typename VariantRef> static constexpr decltype(auto) get(VariantRef &&value) {
        return std::get<I>(std::forward<VariantRef>(value).storage);
    }

    template <typename Visitor, typename... VariantRefs> static constexpr decltype(auto) visit(Visitor &&visitor, VariantRefs &&...values) {
        return std::visit(std::forward<Visitor>(visitor), std::forward<VariantRefs>(values).storage...);
    }

    template <std::size_t I, typename U> static constexpr void assign(variant_type &value, U &&decoded_value) {
        value.storage.template emplace<I>(std::forward<U>(decoded_value));
    }
};

} // namespace cbor::tags

int main() {
    using namespace cbor::tags::ext::rfc8746;

    using value_type = variant_traits_compile_fail_test::manual_variant<typed_array<std::int32_t>, typed_array_view<std::int32_t>>;

    fmt::memory_buffer buffer;
    cbor::tags::cddl_schema_to<value_type>(buffer, {.row_options = {.format_by_rows = false}});
    return 0;
}
