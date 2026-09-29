#include <cbor_tags/expected.h>
#include <cstdlib>
#include <exception>

int main(int argc, char **) {
    using cbor::tags::expected;
    using cbor::tags::unexpected;
    expected<int, int> value(7);
    expected<int, int> error(unexpected(3));
    if (argc > 1) {
        std::set_terminate([] { std::_Exit(42); });
        return error.value();
    }
    value.swap(error);
    if (value.error() != 3 || *error != 7)
        return 1;
    auto result = error.transform([](int n) { return n + 1; });
    if (*result != 8)
        return 2;
    expected<void, int> empty;
    empty = unexpected(4);
    empty.emplace();
    return empty.has_value() ? 0 : 3;
}
