#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <cstddef>
#include <memory>
#include <vector>

struct empty {};

int main() {
    std::vector<std::byte> bytes;
    auto                   enc = cbor::tags::make_encoder<cbor::tags::ext::smart_ptr::shared_ptr_codec>(bytes);

    auto value = std::make_shared<empty>();
    return enc(value).has_value() ? 0 : 1;
}
