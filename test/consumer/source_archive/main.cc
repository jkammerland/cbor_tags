#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cstddef>
#include <vector>

int main() {
    const std::vector<int> input{1, -2, 3};
    std::vector<std::byte> bytes;

    auto encoder = cbor::tags::make_encoder(bytes);
    if (!encoder(input)) {
        return 1;
    }

    std::vector<int> output;
    auto             decoder = cbor::tags::make_decoder(bytes);
    if (!decoder(output) || output != input || decoder.tell() != bytes.end()) {
        return 2;
    }

    return 0;
}
