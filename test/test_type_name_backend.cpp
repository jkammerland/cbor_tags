#include "cbor_tags/detail/type_name.h"

#include <cstddef>
#include <doctest/doctest.h>
#include <ostream>
#include <string>
#include <string_view>

namespace {

struct TypeNameBackendRegression {
    int value{};
};

#if defined(_MSC_VER)
#define CBOR_TAGS_TEST_NOINLINE __declspec(noinline)
#else
#define CBOR_TAGS_TEST_NOINLINE __attribute__((noinline))
#endif

template <typename T> CBOR_TAGS_TEST_NOINLINE std::string short_type_name_after_stack_reuse() {
    auto          name = cbor::tags::detail::short_type_name<T>();
    volatile char noise[4096]{};
    for (std::size_t i = 0; i < sizeof(noise); ++i) {
        noise[i] = static_cast<char>('a' + (i % 26U));
    }
    return std::string{name};
}

#undef CBOR_TAGS_TEST_NOINLINE

} // namespace

namespace type_name_backend_regression {

struct NamespacedType {
    int value{};
};

enum class NamespacedChoice { value };

} // namespace type_name_backend_regression

TEST_CASE("non stl-only type names keep the nameof backend") {
#if !CBOR_TAGS_STL_ONLY
    CHECK_EQ(cbor::tags::detail::short_type_name<TypeNameBackendRegression>(), std::string_view{"TypeNameBackendRegression"});
    CHECK_EQ(cbor::tags::detail::short_type_name<type_name_backend_regression::NamespacedType>(), std::string_view{"NamespacedType"});
    CHECK_EQ(cbor::tags::detail::short_type_name<type_name_backend_regression::NamespacedChoice>(), std::string_view{"NamespacedChoice"});
    CHECK_EQ(short_type_name_after_stack_reuse<type_name_backend_regression::NamespacedType>(), "NamespacedType");
    CHECK_EQ(short_type_name_after_stack_reuse<type_name_backend_regression::NamespacedChoice>(), "NamespacedChoice");

    const auto full_name = cbor::tags::detail::full_type_name<TypeNameBackendRegression>();
    CHECK(full_name.find("TypeNameBackendRegression") != std::string_view::npos);
#else
    CHECK(true);
#endif
}
