#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_resumable.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <span>
#include <string>
#include <vector>

namespace ct = cbor::tags;

struct Message {
    std::string name;
    std::vector<std::uint32_t> values;
};

/**
items = (message, uint)
message = [tstr, [* uint]]
*/
int main() {
    // Encode a record and a separate sequence number as two top-level items.
    const Message sent{"station", {1, 24, 1000}};
    const std::uint32_t sequence = 42;
    std::vector<std::byte> buffer;
    if (!ct::make_encoder(buffer)(sent, sequence)) {
        return 1;
    }

    // The bytes, descriptors, source and destinations all outlive the operation.
    const auto bytes = std::span<const std::byte>{buffer};
    std::list<std::span<const std::byte>> chunks;
    auto source = ct::make_input_source(chunks);
    Message received;
    std::uint32_t received_sequence{};
    auto operation = ct::make_resumable_decoder(source)(received, received_sequence);

    // Supply several chunks in one call; the text payload still needs input.
    if (operation(bytes.first(1), bytes.subspan(1, 1)) != ct::resume_state::need_input ||
        operation.done()) {
        return 2;
    }

    // A runtime batch is also accepted. Only the explicit token signals EOF.
    std::vector<std::span<const std::byte>> batch{bytes.subspan(2, 2), bytes.subspan(4)};
    if (operation(std::span{batch}, ct::end_of_input) != ct::resume_state::done) {
        return 2;
    }

    // Check both destinations and the byte count, including item headers.
    const auto result = operation.result();
    if (!result || *result != buffer.size() || received.name != sent.name ||
        received.values != sent.values || received_sequence != sequence ||
        !operation.remaining_input().empty()) {
        return 3;
    }
    return 0;
}
