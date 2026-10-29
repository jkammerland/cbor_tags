#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_resumable.h"
#include "resumable_consumer.h"

#include <cstddef>
#include <list>
#include <span>
#include <string>
#include <vector>

namespace ct = cbor::tags;

// This is an ordinary C++20 coroutine; it needs no executor or CBOR-specific promise.
template <typename Operation>
example::consumer consume(Operation &operation, std::size_t expected_bytes) {
    const auto result = co_await operation;
    co_return result && *result == expected_bytes;
}

/**
message = tstr
*/
int main() {
    // Prepare bytes whose storage outlives the source and coroutine frames.
    const std::string sent = "ready";
    std::vector<std::byte> buffer;
    if (!ct::make_encoder(buffer)(sent)) {
        return 1;
    }

    // Start the operation, then its waiter; destruction happens in reverse order.
    const auto bytes = std::span<const std::byte>{buffer};
    std::list<std::span<const std::byte>> chunks;
    auto source = ct::make_input_source(chunks);
    std::string received;
    auto operation = ct::make_resumable_decoder(source)(received);
    auto consumer = consume(operation, buffer.size());
    if (consumer.done()) {
        return 2;
    }

    // The producer feeds from the same serialized execution context as the waiter.
    if (operation(bytes.first(2)) != ct::resume_state::need_input || consumer.done()) {
        return 2;
    }

    // Completing the operation resumes the consumer inline before this call returns.
    if (operation(bytes.subspan(2), ct::end_of_input) != ct::resume_state::done) {
        return 2;
    }
    if (!consumer.succeeded() || received != sent) {
        return 3;
    }
    return 0;
}
