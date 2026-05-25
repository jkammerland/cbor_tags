#include "cbor_tags/detail/type_name.h"

#include <doctest/doctest.h>
#include <string_view>

namespace {

struct TypeNameBackendRegression {
    int value{};
};

} // namespace

TEST_CASE("non stl-only type names keep the nameof backend") {
#if !CBOR_TAGS_STL_ONLY
    CHECK_EQ(cbor::tags::detail::short_type_name<TypeNameBackendRegression>(), std::string_view{"TypeNameBackendRegression"});

    const auto full_name = cbor::tags::detail::full_type_name<TypeNameBackendRegression>();
    CHECK(full_name.find("TypeNameBackendRegression") != std::string_view::npos);
#else
    CHECK(true);
#endif
}
