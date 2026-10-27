#include <cbor_tags/cbor_resumable.h>

static_assert(std::ranges::forward_range<cbor::tags::readable_chunk>);

int main() {
    int  value{};
    auto decoder   = cbor::tags::make_resumable_decoder();
    auto operation = decoder(value);
    auto validator = cbor::tags::make_resumable_validator();
    return operation.done() || validator.done() ? 1 : 0;
}
