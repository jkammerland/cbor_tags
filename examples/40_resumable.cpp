#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_resumable.h"
#include "example_support.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iostream>
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
    const Message sent{.name = "station", .values = {1, 24, 1000}};
    const std::uint32_t sequence = 42;
    std::vector<std::byte> buffer;
    const auto encoded = ct::make_encoder(buffer)(sent, sequence);
    assert(encoded);

    // The buffer owns every byte; the deque holds append-only borrowed chunks.
    const auto bytes = std::span<const std::byte>{buffer};
    std::deque<std::span<const std::byte>> input;
    auto decoder = ct::make_resumable_decoder(input);
    Message received;
    std::uint32_t received_sequence{};
    auto operation = decoder(received, received_sequence);

    // This first chunk contains only headers, so the message still needs input.
    const auto first_chunk = bytes.first(2);
    input.push_back(first_chunk);
    const auto first_state = operation.resume();
    assert(first_state == ct::resume_state::need_input);
    std::cout << "first input: " << example::to_hex(first_chunk) << '\n';

    // subspan(2) supplies every byte after the two already supplied.
    const auto last_chunk = bytes.subspan(2);
    input.push_back(last_chunk);
    const auto final_state = operation.resume();
    assert(final_state == ct::resume_state::done);
    std::cout << "remaining input: " << example::to_hex(last_chunk) << '\n';

    // Typed decoding finishes when all requested values are available, without EOF.
    const auto result = operation.result();
    assert(result);
    assert(*result == buffer.size());
    assert(received.name == sent.name);
    assert(received.values == sent.values);
    assert(received_sequence == sequence);
    assert(operation.remaining_input().empty());

    // Keep descriptors and their owner until destruction. Reclaiming consumed
    // chunks during an operation requires a custom source, as in example 46.
    return 0;
}
