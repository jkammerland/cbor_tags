#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_resumable.h"

#include <array>
#include <cstddef>
#include <list>
#include <ranges>
#include <span>
#include <string>

namespace ct = cbor::tags;

/**
message = tstr
*/
int main() {
    // Keep the value and every supplied output buffer alive through completion.
    const std::string sent = "ready for transport";
    std::array<std::byte, 2> first{};
    std::array<std::byte, 64> second{};
    std::list<std::span<std::byte>> chunks;
    auto sink = ct::make_output_source(chunks);
    auto operation = ct::make_resumable_encoder(sink)(sent);

    // Exhausting the first buffer suspends the operation without closing it.
    if (operation(first) != ct::resume_state::need_output || operation.done()) {
        return 1;
    }

    // Add space without reusing or publishing bytes from the pending operation.
    if (operation(second) != ct::resume_state::done) {
        return 1;
    }

    // produced() includes only committed bytes, excluding unused output capacity.
    const auto result = operation.result();
    auto produced = operation.produced();
    if (!result || *result <= first.size() ||
        *result != static_cast<std::size_t>(std::ranges::distance(produced)) ||
        *result + static_cast<std::size_t>(std::ranges::distance(operation.remaining_output())) !=
            first.size() + second.size()) {
        return 2;
    }

    // Decode the committed range while its backing buffers remain alive.
    std::string received;
    auto decoder = ct::make_decoder(produced);
    if (!decoder(received) || received != sent || decoder.tell() != produced.end()) {
        return 3;
    }
    return 0;
}
