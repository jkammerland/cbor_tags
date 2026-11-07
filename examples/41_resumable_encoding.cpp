#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_resumable.h"
#include "example_support.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <deque>
#include <iostream>
#include <span>
#include <string>

namespace ct = cbor::tags;

/**
message = tstr
*/
int main() {
    // Storage owns the bytes; append-only spans expose its available capacity.
    std::array<std::byte, 64> storage;
    std::deque<std::span<std::byte>> output;
    auto encoder = ct::make_resumable_encoder(output);
    const std::string sent = "ready for transport";
    auto operation = encoder(sent);

    output.push_back(std::span{storage}.first(4));
    const auto first_state = operation.resume();
    assert(first_state == ct::resume_state::need_output);

    output.push_back(std::span{storage}.subspan(4));
    const auto final_state = operation.resume();
    assert(final_state == ct::resume_state::finished);

    // Progress counts committed bytes, independently of the storage's capacity.
    const auto result = operation.result();
    assert(result);
    assert(*result == operation.committed_bytes());
    auto committed = std::span<const std::byte>{storage}.first(operation.committed_bytes());

    // Decode the committed prefix directly from the caller-owned container.
    auto decoder = ct::make_decoder(committed);
    std::string received;
    const auto decoded = decoder(received);
    assert(decoded);
    assert(received == sent);
    assert(decoder.tell() == committed.end());

    // Keep the descriptors and storage alive. Publishing does not authorize
    // pop_front(); reclaiming capacity needs a custom output buffer (example 46).

    std::cout << "received: " << received << '\n';
    std::cout << "encoded bytes: " << operation.committed_bytes() << '\n';
    std::cout << "encoded: " << example::to_hex(committed) << '\n';
    return 0;
}
