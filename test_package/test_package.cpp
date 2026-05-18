#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cstddef>
#include <cstdint>
#include <vector>

int main() {
    std::vector<std::byte> bytes;
    auto                   enc = cbor::tags::make_encoder(bytes);
    if (!enc(std::uint64_t{42})) {
        return 1;
    }

    std::uint64_t decoded{};
    auto          dec = cbor::tags::make_decoder(bytes);
    if (!dec(decoded)) {
        return 2;
    }

    return decoded == 42U ? 0 : 3;
}
