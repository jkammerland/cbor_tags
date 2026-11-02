#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cstddef>
#include <vector>

using namespace cbor::tags;

int main() {
    std::vector<std::byte> bytes;
    auto                   encoder = make_encoder(bytes);
#if defined(CBOR_NODISCARD_ENCODE)
    encoder(42);
#else
    const auto encoded = encoder(42);
    if (!encoded)
        return 1;
#endif
    auto decoder = make_decoder(bytes);
    int  value{};
#if defined(CBOR_NODISCARD_DECODE)
    decoder(value);
#else
    const auto decoded = decoder(value);
    if (!decoded || value != 42)
        return 2;
#endif
    // Explicit discarding is still available for callers with their own policy.
    (void)encoder(1);
}
