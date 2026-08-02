#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_simple.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <cstddef>
#include <memory>
#include <vector>

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::smart_ptr;

    auto value = std::make_unique<simple>(static_cast<simple::value_type>(SimpleType::Null));

    std::vector<std::byte> bytes;
    auto                   enc = make_encoder<unique_ptr_codec>(bytes);
    return enc(value).has_value() ? 0 : 1;
}
