#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_resumable.h"

#include <cassert>
#include <cstddef>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace ct = cbor::tags;

/**
items = ([* tstr], bool)
*/
int main() {
    // Prepare two items; validate their CBOR without constructing decoded values.
    const std::vector<std::string> stations{"north", "south"};
    std::vector<std::byte> buffer;
    const auto encoded = ct::make_encoder(buffer)(stations, true);
    assert(encoded);

    // until_end_of_input selects validation of the entire incoming sequence.
    const auto bytes = std::span<const std::byte>{buffer};
    std::vector<std::byte> input;
    auto source = ct::make_input_source(input);
    auto operation = ct::make_resumable_validator(source, ct::until_end_of_input);

    // The producer appends bytes to the bound container before resuming validation.
    input.insert(input.end(), bytes.begin(), bytes.begin() + 3);
    const auto first_state = operation.resume();
    assert(first_state == ct::resume_state::need_input);
    assert(input.size() == 3);
    std::cout << "input bytes: " << input.size() << '\n';

    // Both items are now complete, but another item could still arrive.
    input.insert(input.end(), bytes.begin() + 3, bytes.end());
    const auto second_state = operation.resume();
    assert(second_state == ct::resume_state::need_input);
    assert(input.size() == buffer.size());
    std::cout << "input bytes: " << input.size() << '\n';

    // finish() announces EOF. Temporary exhaustion does not end the sequence.
    const auto final_state = operation.finish();
    assert(final_state == ct::resume_state::done);

    // Success checks CBOR structure and UTF-8, not the application CDDL schema.
    const auto result = operation.result();
    assert(result);
    assert(*result == buffer.size());
    assert(operation.remaining_input().empty());
    return 0;
}
