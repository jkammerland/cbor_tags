#include <cbor_tags/cbor_encoder.h>

#include <cstddef>
#include <vector>

int main() {
    std::vector<std::byte> buffer;
    auto                   enc = cbor::tags::make_encoder(buffer);

    auto result = enc(cbor::tags::as_array_range(std::vector<int>{1, 2, 3}));
    return result ? 0 : 1;
}
