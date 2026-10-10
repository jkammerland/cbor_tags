#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_traversal.h"

#include <concepts>
#include <cstddef>
#include <type_traits>
#include <vector>

using namespace cbor::tags;

/**
nested_arrays = [* [* int]]
*/
int main() {
    // Encode an outer array containing two inner arrays.
    std::vector<std::vector<int>> source{{1, 2}, {3}};
    std::vector<std::byte> buffer;
    auto encoder = make_encoder(buffer);
    if (!encoder(source)) {
        return 1;
    }

    // Count array headers only on entry, not on the matching leave event.
    std::size_t array_count = 0;
    auto count_arrays = [&array_count](const auto &value, const auto &context) {
        using value_type = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<value_type, as_array_any>) {
            if (context.kind == walk_event_kind::enter) {
                ++array_count;
            }
        }
    };

    // Walk the item with the named callback.
    auto decoder = make_decoder(buffer);
    if (!walk_item(decoder, count_arrays)) {
        return 1;
    }

    // Check that all three arrays were visited and the item was consumed.
    if (array_count != 3 || decoder.tell() != buffer.end()) {
        return 2;
    }
    return 0;
}
