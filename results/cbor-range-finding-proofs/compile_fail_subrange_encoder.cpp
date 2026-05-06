#include <cbor_tags/cbor_encoder.h>

#include <cstddef>
#include <ranges>
#include <vector>

int main() {
    std::vector<std::byte> storage(8);
    auto                   out = std::ranges::subrange(storage.begin(), storage.end());

    static_assert(cbor::tags::ValidCborBuffer<decltype(out)>);

    auto enc    = cbor::tags::make_encoder(out);
    auto result = enc(1);
    return result ? 0 : 1;
}
