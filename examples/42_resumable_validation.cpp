#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_resumable.h"

#include <cassert>
#include <cstddef>
#include <iostream>
#include <list>
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
    std::list<std::span<const std::byte>> chunks;
    auto source = ct::make_input_source(chunks);
    auto operation = ct::make_resumable_validator(source, ct::until_end_of_input);

    // Feeding appends a borrowed descriptor to chunks before resuming validation.
    const auto first_state = operation(bytes.first(3));
    assert(first_state == ct::resume_state::need_input);
    assert(chunks.size() == 1);
    std::cout << "input chunks: " << chunks.size() << '\n';

    // Both items are now complete, but another item could still arrive.
    const auto second_state = operation(bytes.subspan(3));
    assert(second_state == ct::resume_state::need_input);
    assert(chunks.size() == 2);
    std::cout << "input chunks: " << chunks.size() << '\n';

    // finish() announces EOF. Empty chunks do not end the sequence.
    const auto final_state = operation.finish();
    assert(final_state == ct::resume_state::done);

    // Success checks CBOR structure and UTF-8, not the application CDDL schema.
    const auto result = operation.result();
    assert(result);
    assert(*result == buffer.size());
    assert(operation.remaining_input().empty());
    return 0;
}
