#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/codec/shared_ptr.h"
#include "cbor_tags/codec/unique_ptr.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

struct record {
    std::uint64_t first{};
    std::uint64_t second{};
};

int main() {
    std::vector<std::byte> bytes;
    auto                   dec = cbor::tags::make_decoder<cbor::tags::codec::unique_ptr>(bytes);

    std::variant<std::unique_ptr<record>, std::vector<std::uint64_t>> decoded;
    return dec(decoded).has_value() ? 0 : 1;
}
