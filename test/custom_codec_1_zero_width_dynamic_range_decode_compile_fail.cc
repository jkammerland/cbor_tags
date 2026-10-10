#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/codec/custom_1.h"

#include <array>
#include <cstddef>
#include <vector>

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::custom_1;

    const std::vector<std::byte>    input;
    std::vector<std::array<int, 0>> values;
    auto                            dec = make_decoder<codec::custom_1>(input);
    return dec(as_ref(static_tag<1>{}, values)).has_value() ? 0 : 1;
}
