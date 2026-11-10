#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <vector>

namespace ct = cbor::tags;

// A hook's one_shot(value) decodes the value again on every retry, so a
// container would get its elements twice
#if defined(CBOR_TAGS_ONE_SHOT_VECTOR)
using hooked = std::vector<int>;
#elif defined(CBOR_TAGS_ONE_SHOT_STRUCT)
struct hooked {
    std::vector<int> values;
};
#endif

template <typename Self> struct one_shot_hook : ct::codec::decoder_base<Self> {
    using base = ct::codec::decoder_base<Self>;
    using base::decode;

    ct::resumable<ct::status_code> resumable_decode(hooked &value) { return static_cast<Self &>(*this).one_shot(value); }
};

int main() {
    std::vector<std::byte> input;
    hooked                 value;
    auto                   decoder   = ct::make_resumable_decoder<one_shot_hook>(input);
    auto                   operation = decoder(value);
    (void)operation;
}
