#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_resumable.h"
#include "resumable_consumer.h"

#include <cassert>
#include <cstddef>
#include <deque>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace ct = cbor::tags;

// consumer is this example's coroutine return type; CBOR needs no special promise.
template <typename Operation>
example::consumer consume(Operation &operation, const std::string &received) {
    // Awaiting yields expected<size_t, status_code>: committed bytes or an error.
    const auto result = co_await operation;
    assert(result);

    // Application work runs as soon as resuming completes the decode.
    std::cout << "received: " << received << '\n';
    co_return static_cast<bool>(result);
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
    std::deque<std::byte> input;
    auto decoder = ct::make_resumable_decoder(input);
    std::string received;
    auto operation = decoder(received);
    auto consumer = consume(operation, received);
    assert(!consumer.done());

    // The producer and waiter run on the same serialized execution context.
    input.insert(input.end(), bytes.begin(), bytes.begin() + 2);
    const auto first_state = operation.resume();
    assert(first_state == ct::resume_state::need_input);
    assert(!consumer.done());

    // This call resumes consume() and prints the decoded message before returning.
    input.insert(input.end(), bytes.begin() + 2, bytes.end());
    const auto final_state = operation.resume();
    assert(final_state == ct::resume_state::done);
    assert(consumer.done());
    assert(consumer.succeeded()); // Checks the example coroutine's returned bool.
    assert(received == sent);

    const auto result = operation.result();
    assert(result);
    assert(*result == buffer.size());
    return 0;
}
