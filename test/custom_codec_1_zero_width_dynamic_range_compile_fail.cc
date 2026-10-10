#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/codec/custom_1.h"

#include <cstddef>
#include <vector>

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::custom_1;

    std::vector<std::byte>      output;
    std::vector<std::nullptr_t> values(1U);
    auto                        enc = make_encoder<codec::custom_1>(output);
    return enc(as_ref(static_tag<1>{}, values)).has_value() ? 0 : 1;
}
