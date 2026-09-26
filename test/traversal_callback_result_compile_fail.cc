#include <cbor_tags/cbor_traversal.h>
#include <cstddef>
#include <vector>

int main() {
    const std::vector<std::byte> input;
    auto                         dec = cbor::tags::make_decoder(input);
    (void)cbor::tags::walk_item(dec, [](const auto &, const auto &) { return true; });
}
