#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_resumable.h"
#include "example_support.h"

#include <cassert>
#include <cstddef>
#include <deque>
#include <iostream>
#include <ranges>
#include <string>

namespace ct = cbor::tags;

/**
message = tstr
*/
int main() {
    // An ordinary deque grows as the encoder appends bytes.
    const std::string sent = "ready for transport";
    std::deque<std::byte> output;
    auto encoder = ct::make_resumable_encoder(output);
    auto operation = encoder(sent);
    assert(operation.done());

    // Progress counts committed bytes, independently of the storage's capacity.
    const auto result = operation.result();
    assert(result);
    assert(*result == operation.committed_bytes());
    auto committed = output | std::views::take(operation.committed_bytes());

    // Decode the committed prefix directly from the caller-owned container.
    auto decoder = ct::make_decoder(committed);
    std::string received;
    const auto decoded = decoder(received);
    assert(decoded);
    assert(received == sent);
    assert(decoder.tell() == committed.end());

    std::cout << "received: " << received << '\n';
    std::cout << "encoded bytes: " << operation.committed_bytes() << '\n';
    std::cout << "encoded: " << example::to_hex(committed) << '\n';
    return 0;
}
