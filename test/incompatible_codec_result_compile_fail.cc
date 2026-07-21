#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cstddef>
#include <vector>

using cbor::tags::expected;

#if defined(CBOR_TAGS_INCOMPATIBLE_MEMBER_ENCODE)
struct incompatible_codec {
    int value{};

    template <typename Encoder> auto encode(Encoder &) const { return expected<void, int>{}; }
};
#elif defined(CBOR_TAGS_INCOMPATIBLE_MEMBER_DECODE)
struct incompatible_codec {
    int value{};

    template <typename Decoder> auto decode(Decoder &) { return expected<void, int>{}; }
};
#elif defined(CBOR_TAGS_INCOMPATIBLE_FREE_ENCODE)
struct incompatible_codec {
    int value{};
};

template <typename Encoder> auto encode(Encoder &, const incompatible_codec &) { return expected<void, int>{}; }
#elif defined(CBOR_TAGS_INCOMPATIBLE_FREE_DECODE)
struct incompatible_codec {
    int value{};
};

template <typename Decoder> auto decode(Decoder &, incompatible_codec &&) { return expected<void, int>{}; }
#else
#error "expected an incompatible-codec compile-fail mode"
#endif

int main() {
#if defined(CBOR_TAGS_INCOMPATIBLE_MEMBER_ENCODE) || defined(CBOR_TAGS_INCOMPATIBLE_FREE_ENCODE)
    std::vector<std::byte> buffer;
    auto                   enc    = cbor::tags::make_encoder(buffer);
    auto                   result = enc(incompatible_codec{});
#else
    const std::vector<std::byte> input{std::byte{0x00}};
    incompatible_codec           value{};
    auto                         dec    = cbor::tags::make_decoder(input);
    auto                         result = dec(value);
#endif
    (void)result;
    return 0;
}
