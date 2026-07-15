#include "cbor_tags/cbor_decoder.h"

#include <cstddef>
#include <vector>

int main() {
    using bounded_value = cbor::tags::dynamic_bounded_size<std::vector<int>>;

    std::vector<std::byte>     input;
    std::vector<bounded_value> output;
    return cbor::tags::make_decoder(input)(output).has_value() ? 0 : 1;
}
