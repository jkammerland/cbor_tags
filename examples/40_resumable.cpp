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

    // The decoder borrows an ordinary container and keeps its logical position.
    const auto bytes = std::span<const std::byte>{buffer};
    std::deque<std::byte> input;
    auto decoder = ct::make_resumable_decoder(input);
    Message received;
    std::uint32_t received_sequence{};
    auto operation = decoder(received, received_sequence);

    // This first chunk contains only headers, so the message still needs input.
    const auto first_chunk = bytes.first(2);
    input.insert(input.end(), first_chunk.begin(), first_chunk.end());
    const auto first_state = operation.resume();
    assert(first_state == ct::resume_state::need_input);
    assert(input.size() == first_chunk.size());
    std::cout << "first input: " << example::to_hex(first_chunk) << '\n';

    // subspan(2) supplies every byte after the two already supplied.
    const auto last_chunk = bytes.subspan(2);
    input.insert(input.end(), last_chunk.begin(), last_chunk.end());
    const auto final_state = operation.resume();
    assert(final_state == ct::resume_state::done);
    assert(input.size() == buffer.size());
    std::cout << "remaining input: " << example::to_hex(last_chunk) << '\n';

    // Typed decoding finishes when all requested values are available, without EOF.
    const auto result = operation.result();
    assert(result);
    assert(*result == buffer.size());
    assert(received.name == sent.name);
    assert(received.values == sent.values);
    assert(received_sequence == sequence);
    assert(operation.remaining_input().empty());
    return 0;
}
