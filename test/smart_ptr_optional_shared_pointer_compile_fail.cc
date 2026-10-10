#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/codec/shared_ptr.h"
#include "cbor_tags/codec/unique_ptr.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

int main() {
    using namespace cbor::tags::smart_ptr;

    std::vector<std::byte> bytes;
    auto                   enc = cbor::tags::make_encoder<cbor::tags::codec::shared_ptr>(bytes);

    std::optional<std::shared_ptr<int>> value{std::make_shared<int>(1)};
    return enc(value).has_value() ? 0 : 1;
}
