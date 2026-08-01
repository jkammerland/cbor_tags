#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

struct tagged_record {
    cbor::tags::static_tag<42> cbor_tag;
    std::uint64_t              first{};
    std::uint64_t              second{};
};

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::smart_ptr;

    std::vector<std::byte> bytes;
    encoder<std::vector<std::byte>, Options<default_expected>, cbor_header_encoder, cbor_indefinite_encoder, cbor_optional_encoder,
            cbor_variant_encoder, shared_ptr_codec>
        enc{bytes};
    return enc(std::make_shared<tagged_record>()).has_value() ? 0 : 1;
}
