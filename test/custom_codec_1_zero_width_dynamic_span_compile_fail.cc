#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/codec/custom_1.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

int main() {
    using namespace cbor::tags;
    using namespace cbor::tags::custom_1;

    std::array<std::nullptr_t, 1> storage{nullptr};
    std::span<std::nullptr_t>     values{storage};
    std::vector<std::byte>        output;
    auto                          enc = make_encoder<codec::custom_1>(output);
    return enc(as_ref(static_tag<1>{}, values)).has_value() ? 0 : 1;
}
