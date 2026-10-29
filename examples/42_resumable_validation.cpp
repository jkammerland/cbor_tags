#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_resumable.h"

#include <cstddef>
#include <list>
#include <span>
#include <string>
#include <vector>

namespace ct = cbor::tags;

/**
items = ([* tstr], bool)
*/
int main() {
    // Prepare two items; the receiver will validate their CBOR without destinations.
    const std::vector<std::string> stations{"north", "south"};
    std::vector<std::byte> buffer;
    if (!ct::make_encoder(buffer)(stations, true)) {
        return 1;
    }

    // Borrow the immutable bytes and validate every item up to explicit EOF.
    const auto bytes = std::span<const std::byte>{buffer};
    std::list<std::span<const std::byte>> chunks;
    auto source = ct::make_input_source(chunks);
    auto operation = ct::make_resumable_validator(source, ct::until_end_of_input);
    if (operation(bytes.first(3)) != ct::resume_state::need_input) {
        return 2;
    }

    // Complete items alone do not end a sequence: another item could still arrive.
    if (operation(bytes.subspan(3)) != ct::resume_state::need_input || operation.done()) {
        return 2;
    }

    // Validation checks CBOR structure and UTF-8, not the application CDDL schema.
    if (operation.finish() != ct::resume_state::done) {
        return 2;
    }
    const auto result = operation.result();
    if (!result || *result != buffer.size() || !operation.remaining_input().empty()) {
        return 3;
    }
    return 0;
}
