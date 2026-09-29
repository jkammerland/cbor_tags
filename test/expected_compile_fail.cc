#include <cbor_tags/detail/expected.h>
#include <memory>

namespace ex = cbor::tags::detail::expected_impl;

int main() {
#if defined(EXPECTED_INVALID_VALUE_REFERENCE)
    ex::expected<int &, int> invalid;
#elif defined(EXPECTED_INVALID_VALUE_ARRAY)
    ex::expected<int[2], int> invalid;
#elif defined(EXPECTED_INVALID_VALUE_TAG)
    ex::expected<std::in_place_t, int> invalid;
#elif defined(EXPECTED_INVALID_ERROR_CONST)
    ex::expected<int, const int> invalid;
#elif defined(EXPECTED_INVALID_ERROR_VOID)
    ex::expected<int, void> invalid;
#elif defined(EXPECTED_INVALID_ERROR_UNEXPECTED)
    ex::expected<int, ex::unexpected<int>> invalid;
#elif defined(EXPECTED_INVALID_UNEXPECTED_REFERENCE)
    ex::unexpected<int &> invalid(0);
#elif defined(EXPECTED_AND_THEN_NOT_EXPECTED)
    ex::expected<int, int> value;
    value.and_then([](int) { return 2; });
#elif defined(EXPECTED_AND_THEN_ERROR_TYPE)
    ex::expected<void, int> value;
    value.and_then([] { return ex::expected<int, long>(2); });
#elif defined(EXPECTED_OR_ELSE_VALUE_TYPE)
    ex::expected<int, int> value;
    value.or_else([](int) { return ex::expected<long, int>(2); });
#elif defined(EXPECTED_TRANSFORM_REFERENCE)
    ex::expected<int, int> value;
    value.transform([](int &n) -> int & { return n; });
#elif defined(EXPECTED_TRANSFORM_ERROR_VOID)
    ex::expected<void, int> value;
    value.transform_error([](int) {});
#elif defined(EXPECTED_VALUE_MOVE_ONLY_ERROR)
    ex::expected<int, std::unique_ptr<int>> value;
    static_cast<void>(std::move(value).value());
#else
#error Select an expected contract violation
#endif
}
