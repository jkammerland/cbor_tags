#include <cbor_tags/cbor_decoder.h>
#include <cstddef>
#include <deque>
#include <map>
#include <optional>
#include <tuple>
#include <variant>
#include <vector>

using namespace cbor::tags;

struct empty {};
struct nested_empty {
    empty member;
};
struct empty_less {
    bool operator()(const empty &, const empty &) const { return false; }
};
struct tagged_array {
    static constexpr std::uint64_t cbor_tag = 42;
    std::deque<empty>              values;
};

#if defined(CBOR_CONTAINER_ARRAY_NESTED_EMPTY)
using destination_type = std::deque<nested_empty>;
#elif defined(CBOR_CONTAINER_ARRAY_TUPLE_EMPTY)
using destination_type = std::deque<std::tuple<empty>>;
#elif defined(CBOR_CONTAINER_ARRAY_UNWRAPPED_EMPTY)
using destination_type = std::deque<std::tuple<empty, empty>>;
#elif defined(CBOR_CONTAINER_MAP_EMPTY)
using destination_type = std::multimap<empty, empty, empty_less>;
#elif defined(CBOR_CONTAINER_OPTIONAL_ARRAY_EMPTY)
using destination_type = std::optional<std::deque<empty>>;
#elif defined(CBOR_CONTAINER_VARIANT_ARRAY_EMPTY)
using destination_type = std::variant<unsigned, tagged_array>;
#else
using destination_type = std::deque<empty>;
#endif

int main() {
    destination_type       value;
    std::vector<std::byte> bytes;

#if defined(CBOR_CONTAINER_ARRAY_UNWRAPPED_EMPTY)
    auto dec = make_decoder_with_options<Options<default_expected>>(bytes);
#else
    auto dec = make_decoder(bytes);
#endif
    return dec(value).has_value() ? 0 : 1;
}
