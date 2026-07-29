#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct record {
    std::uint64_t id{};
    std::string   name;
};

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::smart_ptr;

    std::vector<std::byte> bytes;
    encoder<std::vector<std::byte>, Options<default_expected>, cbor_header_encoder, cbor_indefinite_encoder, cbor_optional_encoder,
            cbor_variant_encoder, shared_ptr_codec>
        enc{bytes};

    auto value = std::make_shared<record>();
    return enc(value).has_value() ? 0 : 1;
}
