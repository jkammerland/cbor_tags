#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_resumable.h"
#include "resumable_consumer.h"

#include <cassert>
#include <cstddef>
#include <iostream>
#include <list>
#include <span>
#include <string>
#include <vector>

namespace ct = cbor::tags;

// consumer is this example's coroutine return type; CBOR needs no special promise.
template <typename Operation>
example::consumer consume(Operation &operation, const std::string &received) {
    // Awaiting yields expected<size_t, status_code>: committed bytes or an error.
    const auto result = co_await operation;
    if (!result) {
        co_return false;
    }

    // Application work runs as soon as feeding input completes the decode.
    std::cout << "received: " << received << '\n';
    co_return true; // Store success in the example consumer's promise.
}

/**
message = tstr
*/
int main() {
    const std::string sent = "ready";
    std::vector<std::byte> buffer;
    const auto encoded = ct::make_encoder(buffer)(sent);
    assert(encoded);

    // Declare the waiter last so its frame is destroyed before the operation.
    const auto bytes = std::span<const std::byte>{buffer};
    std::list<std::span<const std::byte>> chunks;
    auto decoder = ct::make_resumable_decoder(chunks);
    std::string received;
    auto operation = decoder(received);
    auto consumer = consume(operation, received);
    assert(!consumer.done());

    // The producer and waiter run on the same serialized execution context.
    const auto first_state = operation(bytes.first(2));
    assert(first_state == ct::resume_state::need_input);
    assert(!consumer.done());

    // This call resumes consume() and prints the decoded message before returning.
    const auto final_state = operation(bytes.subspan(2));
    assert(final_state == ct::resume_state::done);
    assert(consumer.done());
    assert(consumer.succeeded()); // Checks the example coroutine's returned bool.
    assert(received == sent);

    const auto result = operation.result();
    assert(result);
    assert(*result == buffer.size());
    return 0;
}
