#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <span>
#include <vector>

namespace ct = cbor::tags;

struct empty {};

int main() {
    std::vector<std::byte> input;

#if defined(CBOR_TAGS_SINK_BOUNDED)
    auto sink = ct::as_bounded_size<0, 4>(ct::as_bstr_sink([](std::span<const std::byte>) {}));
#elif defined(CBOR_TAGS_SINK_EMPTY_ELEMENT)
    auto sink = ct::as_array_sink<empty>([](empty) {});
#elif defined(CBOR_TAGS_SINK_EMPTY_ENTRY)
    auto sink = ct::as_map_sink<empty, empty>([](empty, empty) {});
#endif

#if defined(CBOR_TAGS_SINK_ONE_SHOT)
    (void)ct::make_decoder(input)(sink);
#elif defined(CBOR_TAGS_SINK_RESUMABLE)
    auto decoder   = ct::make_resumable_decoder(input);
    auto operation = decoder(sink);
    (void)operation;
#endif
}
