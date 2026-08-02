#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

int main() {
    std::vector<std::byte> bytes;
    auto                   enc = cbor::tags::make_encoder<cbor::tags::ext::smart_ptr::unique_ptr_codec>(bytes);

    std::optional<std::unique_ptr<int>> value{std::make_unique<int>(1)};
    return enc(value).has_value() ? 0 : 1;
}
