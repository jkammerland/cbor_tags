#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

int main() {
    std::vector<std::byte> bytes;
    auto                   dec = cbor::tags::make_decoder<cbor::tags::ext::smart_ptr::shared_graph_codec>(bytes);

    cbor::tags::ext::smart_ptr::shared_graph_decode_session                               graph;
    std::variant<std::vector<std::shared_ptr<std::uint64_t>>, std::vector<std::uint64_t>> decoded;
    return dec(cbor::tags::ext::smart_ptr::as_shared_graph(graph, decoded)).has_value() ? 0 : 1;
}
