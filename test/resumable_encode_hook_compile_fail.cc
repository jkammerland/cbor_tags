#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <vector>

namespace ct = cbor::tags;

// Every hook here only takes a mutable point. A const point would skip it and
// get its fields written as x, y instead
struct point {
    int x{};
    int y{};

#if defined(CBOR_TAGS_ENCODE_HOOK_MEMBER_TRANSCODE)
    template <typename T> ct::resumable<ct::status_code> resumable_transcode(T &transcoder) { return transcoder(y, x); }
#endif
};

#if defined(CBOR_TAGS_ENCODE_HOOK_ADL_ENCODE)
template <typename E> ct::resumable<ct::status_code> resumable_encode(E &encoder, point &value) { return encoder(value.y, value.x); }
#elif defined(CBOR_TAGS_ENCODE_HOOK_ADL_TRANSCODE)
template <typename T> ct::resumable<ct::status_code> resumable_transcode(T &transcoder, point &value) {
    return transcoder(value.y, value.x);
}
#endif

template <typename Self> struct point_hook : ct::codec::encoder_base<Self> {
    using base = ct::codec::encoder_base<Self>;
    using base::encode;

#if defined(CBOR_TAGS_ENCODE_HOOK_MIXIN_ENCODE)
    ct::resumable<ct::status_code> resumable_encode(point &value) { return static_cast<Self &>(*this)(value.y, value.x); }
#endif
};

int main() {
    std::vector<std::byte> output;
    const point            value{1, 2};
    auto                   encoder   = ct::make_resumable_encoder<point_hook>(output);
    auto                   operation = encoder(value);
    (void)operation;
}
