#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

int main() {
    std::vector<std::byte> bytes;
    auto                   dec = cbor::tags::make_decoder<cbor::tags::ext::smart_ptr::nullable_ptr_codec>(bytes);

    std::variant<std::unique_ptr<std::uint64_t>, std::shared_ptr<std::uint64_t>> decoded;
    return dec(decoded).has_value() ? 0 : 1;
}
