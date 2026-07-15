#include "cbor_tags/cbor_variant_traits.h"

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <variant>

namespace variant_traits_compile_fail_test {

struct throwing_index_variant {
    std::variant<std::uint64_t, bool> storage;
};

} // namespace variant_traits_compile_fail_test

namespace cbor::tags {

template <> struct variant_traits<variant_traits_compile_fail_test::throwing_index_variant> {
    using variant_type = variant_traits_compile_fail_test::throwing_index_variant;

    static constexpr std::size_t size = 2U;

    template <std::size_t I> using alternative = std::tuple_element_t<I, std::tuple<std::uint64_t, bool>>;

    [[nodiscard]] static std::size_t index(const variant_type &value) { return value.storage.index(); }
};

} // namespace cbor::tags

int main() {
    variant_traits_compile_fail_test::throwing_index_variant value;
    return static_cast<int>(cbor::tags::detail::variant_index(value));
}
