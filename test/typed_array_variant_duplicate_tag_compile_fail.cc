#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/extensions/rfc8746_typed_arrays.h"

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

int main() {
    std::vector<std::byte> bytes;
    auto                   dec = cbor::tags::make_decoder<cbor::tags::ext::rfc8746::typed_array_codec>(bytes);

    std::variant<cbor::tags::ext::rfc8746::typed_array<std::int32_t>, cbor::tags::ext::rfc8746::typed_array_view<std::int32_t>> decoded;
    return dec(decoded).has_value() ? 0 : 1;
}
