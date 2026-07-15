#include "cbor_tags/cbor_decoder.h"

#include <cstddef>
#include <optional>
#include <vector>

int main() {
    using bounded_value = cbor::tags::dynamic_bounded_size<std::vector<int>>;

    std::vector<std::byte>       input;
    std::optional<bounded_value> output{std::in_place, std::vector<int>{}, 0, 4};
    return cbor::tags::make_decoder(input)(output).has_value() ? 0 : 1;
}
