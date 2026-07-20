#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/extensions/rfc8746_typed_arrays.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::ext::rfc8746;

    std::vector<std::byte>                                                              input;
    auto                                                                                dec = make_decoder<typed_array_codec>(input);
    bounded_size<typed_array<std::int32_t>, 0, std::numeric_limits<std::size_t>::max()> value;
    return dec(value).has_value() ? 0 : 1;
}
