#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_resumable.h"
#include "resumable_consumer.h"

#include <array>
#include <cstddef>
#include <list>
#include <span>
#include <vector>

namespace ct = cbor::tags;

template <typename Generator>
example::consumer collect(Generator &items, std::vector<int> &received, std::size_t &consumed) {
    for (;;) {
        // next() returns a reference to the operation owned by the generator.
        const auto result = co_await items.next();
        if (!result) {
            co_return false;
        }
        if (items.done()) {
            co_return *result == 0;
        }

        // Copy each value before next() replaces the generator's current value.
        consumed += *result;
        received.push_back(items.value());
    }
}

/**
readings = (uint, uint, uint)
*/
int main() {
    // Encode three top-level readings, without an enclosing CBOR array.
    const std::array sent{7, 24, 1000};
    std::vector<std::byte> buffer;
    if (!ct::make_encoder(buffer)(sent[0], sent[1], sent[2])) {
        return 1;
    }

    // The consumer outlives no borrowed object: it is declared last and destroyed first.
    const auto bytes = std::span<const std::byte>{buffer};
    std::list<std::span<const std::byte>> chunks;
    auto source = ct::make_input_source(chunks);
    auto items = ct::make_resumable_generator<int>(source);
    std::vector<int> received;
    std::size_t consumed{};
    auto consumer = collect(items, received, consumed);

    // One feed can yield an item and immediately start waiting for the next one.
    items(bytes.first(1));
    if (consumer.done() || items.done() || received != std::vector<int>{sent.front()}) {
        return 2;
    }

    // Buffered items drain inline; the consumer still waits for the end of the sequence.
    items(bytes.subspan(1));
    if (consumer.done() || received != std::vector<int>(sent.begin(), sent.end())) {
        return 2;
    }

    // Clean EOF produces a successful zero-byte result, not another reading.
    items(ct::end_of_input);
    if (!consumer.succeeded() || !items.done() || consumed != buffer.size()) {
        return 3;
    }
    return 0;
}
