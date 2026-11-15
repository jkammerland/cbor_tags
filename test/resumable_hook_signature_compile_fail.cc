#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <utility>
#include <vector>

namespace ct = cbor::tags;

// Each hook here is spelled almost right. It compiles, nothing calls it, and
// the record would be read or written field by field instead
struct record {
    int id{};
    int size{};

#if defined(CBOR_TAGS_HOOK_SIGNATURE_MEMBER_ENCODE)
    // Not const
    template <typename E> ct::resumable<ct::status_code> resumable_encode(E &encoder) { return encoder(size, id); }
#elif defined(CBOR_TAGS_HOOK_SIGNATURE_RETURN_ENCODE)
    // Not resumable<status_code>
    template <typename E> ct::resumable<bool> resumable_encode(E &encoder) const {
        co_return co_await encoder(size, id) == ct::status_code::success;
    }
#endif
};

#if defined(CBOR_TAGS_HOOK_SIGNATURE_RVALUE_DECODE)
// The one-shot decode(Decoder &, T &&) shape
template <typename D> ct::resumable<ct::status_code> resumable_decode(D &decoder, record &&value) { return decoder(value.size, value.id); }
#elif defined(CBOR_TAGS_HOOK_SIGNATURE_RETURN_DECODE)
template <typename D> ct::resumable<bool> resumable_decode(D &decoder, record &value) {
    co_return co_await decoder(value.size, value.id) == ct::status_code::success;
}
#elif defined(CBOR_TAGS_HOOK_SIGNATURE_RVALUE_TRANSCODE_DECODE) || defined(CBOR_TAGS_HOOK_SIGNATURE_RVALUE_TRANSCODE_ENCODE)
template <typename T> ct::resumable<ct::status_code> resumable_transcode(T &transcoder, record &&value) {
    return transcoder(value.size, value.id);
}
#endif

int main() {
    std::vector<std::byte> bytes;
    record                 value;

#if defined(CBOR_TAGS_HOOK_SIGNATURE_MEMBER_ENCODE) || defined(CBOR_TAGS_HOOK_SIGNATURE_RETURN_ENCODE) ||                                  \
    defined(CBOR_TAGS_HOOK_SIGNATURE_RVALUE_TRANSCODE_ENCODE)
    auto encoder   = ct::make_resumable_encoder(bytes);
    auto operation = encoder(value);
#else
    auto decoder   = ct::make_resumable_decoder(bytes);
    auto operation = decoder(value);
#endif
    (void)operation;
}
