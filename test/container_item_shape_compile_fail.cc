#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cstddef>
#include <deque>
#include <map>
#include <vector>

using namespace cbor::tags;

struct empty {};
struct empty_less {
    bool operator()(const empty &, const empty &) const { return false; }
};
struct group {
    int first{};
    int second{};
};

#if defined(CBOR_CONTAINER_ARRAY_GROUP)
using container_type = std::vector<group>;
using shape_options  = Options<default_expected>;
#elif defined(CBOR_CONTAINER_ARRAY_HEADER)
using container_type = std::vector<as_array_any>;
using shape_options  = default_options;
#elif defined(CBOR_CONTAINER_ARRAY_FIXED)
using container_type = std::array<empty, 1>;
using shape_options  = default_options;
#else
using shape_options = default_options;
#if defined(CBOR_CONTAINER_MAP_KEY) || defined(CBOR_CONTAINER_BOUNDED_MAP_KEY) || defined(CBOR_CONTAINER_DYNAMIC_BOUNDED_MAP_KEY) ||       \
    defined(CBOR_CONTAINER_INDEFINITE_MAP_KEY)
using container_type = std::map<empty, int, empty_less>;
#elif defined(CBOR_CONTAINER_MAP_VALUE) || defined(CBOR_CONTAINER_BOUNDED_MAP_VALUE) ||                                                    \
    defined(CBOR_CONTAINER_DYNAMIC_BOUNDED_MAP_VALUE) || defined(CBOR_CONTAINER_INDEFINITE_MAP_VALUE) ||                                   \
    defined(CBOR_CONTAINER_BOUNDED_INDEFINITE_MAP)
using container_type = std::map<int, empty>;
#else
// A non-reserving container demonstrates the zero-width decode vulnerability.
using container_type = std::deque<empty>;
#endif
#endif

int main() {
    container_type container;

#if defined(CBOR_CONTAINER_BOUNDED_ARRAY) || defined(CBOR_CONTAINER_BOUNDED_MAP_KEY) || defined(CBOR_CONTAINER_BOUNDED_MAP_VALUE)
    auto value = bounded_size<container_type, 0, 1>{container};
#elif defined(CBOR_CONTAINER_DYNAMIC_BOUNDED_ARRAY) || defined(CBOR_CONTAINER_DYNAMIC_BOUNDED_MAP_KEY) ||                                  \
    defined(CBOR_CONTAINER_DYNAMIC_BOUNDED_MAP_VALUE)
    auto value = dynamic_bounded_size<container_type>{container, 0, 1};
#elif defined(CBOR_CONTAINER_INDEFINITE_ARRAY) || defined(CBOR_CONTAINER_INDEFINITE_MAP_KEY) || defined(CBOR_CONTAINER_INDEFINITE_MAP_VALUE)
    auto value = as_indefinite{container};
#elif defined(CBOR_CONTAINER_BOUNDED_INDEFINITE_ARRAY) || defined(CBOR_CONTAINER_BOUNDED_INDEFINITE_MAP)
    auto indefinite = as_indefinite{container};
    auto value      = bounded_size<decltype(indefinite), 0, 1>{indefinite};
#else
    auto &value = container;
#endif

    std::vector<std::byte> bytes;
#if defined(CBOR_CONTAINER_ENCODE)
    encoder<std::vector<std::byte>, shape_options, cbor_header_encoder, cbor_indefinite_encoder, cbor_optional_encoder,
            cbor_variant_encoder>
        enc{bytes};
    return enc(value).has_value() ? 0 : 1;
#else
    auto dec = make_decoder_with_options<shape_options>(bytes);
    return dec(value).has_value() ? 0 : 1;
#endif
}
