#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/extensions/custom_codec_1.h"

#include <array>
#include <cstddef>
#include <vector>

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::custom_codec_1;

    const std::vector<std::byte>    input;
    std::vector<std::array<int, 0>> values;
    auto                            dec = make_decoder<custom_codec_1>(input);
    return dec(as_custom_codec_1(static_tag<1>{}, values)).has_value() ? 0 : 1;
}
