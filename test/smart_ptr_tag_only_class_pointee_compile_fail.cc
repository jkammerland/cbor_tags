#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <cstddef>
#include <memory>
#include <vector>

struct tag_only_record {
    cbor::tags::static_tag<42> cbor_tag;
};

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::smart_ptr;

    std::vector<std::byte> bytes;
    auto                   enc = make_encoder<shared_ptr_codec>(bytes);
    return enc(std::make_shared<tag_only_record>()).has_value() ? 0 : 1;
}
