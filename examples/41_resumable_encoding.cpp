#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_resumable.h"
#include "example_support.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <list>
#include <ranges>
#include <span>
#include <string>

namespace ct = cbor::tags;

/**
message = tstr
*/
int main() {
    // This text needs 20 encoded bytes. Two bytes deliberately force suspension.
    const std::string sent = "ready for transport";
    std::array<std::byte, 2> first{};
    std::array<std::byte, 64> second{};

    // The named encoder owns its cursor state and borrows the output buffers.
    std::list<std::span<std::byte>> chunks;
    auto encoder = ct::make_resumable_encoder(chunks);
    auto operation = encoder(sent);

    // Supply writable space, then observe that the encoder needs more.
    const auto first_state = operation(first);
    assert(first_state == ct::resume_state::need_output);
    assert(chunks.size() == 1);

    const auto final_state = operation(second);
    assert(final_state == ct::resume_state::done);
    assert(chunks.size() == 2);

    // produced() excludes unused capacity and any uncommitted writes.
    const auto result = operation.result();
    assert(result);
    auto produced = operation.produced();
    const auto written = static_cast<std::size_t>(std::ranges::distance(produced));
    const auto unused =
        static_cast<std::size_t>(std::ranges::distance(operation.remaining_output()));
    assert(*result > first.size());
    assert(*result == written);
    assert(written + unused == first.size() + second.size());

    // Decode the committed bytes while both backing buffers and the encoder are alive.
    std::string received;
    auto decoder = ct::make_decoder(produced);
    const auto decoded = decoder(received);
    assert(decoded);
    assert(received == sent);
    assert(decoder.tell() == produced.end());

    std::cout << "received: " << received << '\n';
    std::cout << "encoded bytes: " << written << '\n';
    std::cout << "encoded: " << example::to_hex(produced) << '\n';
    return 0;
}
