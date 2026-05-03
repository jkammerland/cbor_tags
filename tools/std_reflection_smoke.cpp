#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_reflection.h"

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <vector>

struct ReflectionSmoke {
    int           id;
    std::uint64_t value;
};

static_assert(CBOR_TAGS_HAS_STD_REFLECTION == 1);
static_assert(cbor::tags::detail::aggregate_binding_count<ReflectionSmoke> == 2);
static_assert(cbor::tags::detail::MAX_REFLECTION_MEMBERS > 1024);

int main() {
    ReflectionSmoke smoke{.id = 7, .value = 42};
    auto            tuple = cbor::tags::to_tuple(smoke);
    std::get<0>(tuple)    = 11;
    if (smoke.id != 11) {
        return 1;
    }

    std::vector<std::byte> buffer;
    auto                   enc = cbor::tags::make_encoder(buffer);
    if (!enc(smoke)) {
        return 2;
    }

    ReflectionSmoke decoded{};
    auto            dec = cbor::tags::make_decoder(buffer);
    if (!dec(decoded)) {
        return 3;
    }

    return decoded.id == 11 && decoded.value == 42 ? 0 : 4;
}
