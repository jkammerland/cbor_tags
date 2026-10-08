#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/codec/shared_ptr.h"
#include "cbor_tags/codec/unique_ptr.h"

#include <cstddef>
#include <memory>
#include <vector>

struct empty {};

int main() {
    std::vector<std::byte> bytes;
    auto                   enc = cbor::tags::make_encoder<cbor::tags::codec::shared_ptr>(bytes);

    auto value = std::make_shared<empty>();
    return enc(value).has_value() ? 0 : 1;
}
