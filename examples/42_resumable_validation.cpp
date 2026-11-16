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

    // A validator checks one item. The producer appends bytes to the bound container before resuming it.
    const auto bytes = std::span<const std::byte>{buffer};
    std::vector<std::byte> input;
    auto source = ct::make_input_source(input);
    std::size_t validated{};
    {
        auto operation = ct::make_resumable_validator(source);
        input.insert(input.end(), bytes.begin(), bytes.begin() + 3);
        const auto first_state = operation.resume();
        assert(first_state == ct::resume_state::need_input);
        std::cout << "input bytes: " << input.size() << '\n';

        input.insert(input.end(), bytes.begin() + 3, bytes.end());
        const auto final_state = operation.resume();
        assert(final_state == ct::resume_state::done);
        std::cout << "input bytes: " << input.size() << '\n';

        // Success checks CBOR structure and UTF-8, not the application CDDL schema.
        const auto result = operation.result();
        assert(result);
        validated += *result;
    }

    // The next validator starts where the previous one stopped. The second item is already here.
    for (;;) {
        auto operation = ct::make_resumable_validator(source);
        if (!operation.done()) {
            // Waiting for another item. None of it has arrived, so if the input ends here the sequence is complete.
            assert(operation.committed_bytes() == 0);
            assert(operation.remaining_input().empty());
            break;
        }
        const auto result = operation.result();
        assert(result);
        validated += *result;
    }
    assert(validated == buffer.size());
    return 0;
}
