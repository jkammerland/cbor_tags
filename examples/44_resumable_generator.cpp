#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_resumable.h"
#include "resumable_consumer.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <span>
#include <vector>

namespace ct = cbor::tags;

template <typename Generator>
example::consumer collect(Generator &items, std::vector<int> &received, std::size_t &consumed) {
    // next() returns the generator-owned operation for the next value or clean EOF.
    auto result = co_await items.next();
    assert(result);
    while (!items.done()) {
        consumed += *result;
        received.push_back(items.value());

        // Copy the value before requesting the next one.
        result = co_await items.next();
        assert(result);
    }
    co_return result && *result == 0;
}

/**
readings = (uint, uint, uint)
*/
int main() {
    // Encode three top-level readings, without an enclosing CBOR array.
    const std::array sent{7, 24, 1000};
    std::vector<std::byte> buffer;
    const auto encoded = ct::make_encoder(buffer)(sent[0], sent[1], sent[2]);
    assert(encoded);

    // The consumer outlives no borrowed object: it is declared last and destroyed first.
    const auto bytes = std::span<const std::byte>{buffer};
    std::vector<std::byte> input;
    auto source = ct::make_input_source(input);
    auto items = ct::make_resumable_generator<int>(source);
    std::vector<int> received;
    std::size_t consumed{};
    auto consumer = collect(items, received, consumed);

    // One resume can yield an item and start waiting for the next one.
    input.push_back(bytes.front());
    items.resume();
    assert(!consumer.done());
    assert(!items.done());
    assert(received == std::vector<int>{sent.front()});

    // Buffered items drain inline; the consumer still waits for the end of the sequence.
    input.insert(input.end(), bytes.begin() + 1, bytes.end());
    items.resume();
    assert(!consumer.done());
    assert(received == std::vector<int>(sent.begin(), sent.end()));

    // Clean EOF produces a successful zero-byte result, not another reading.
    items.finish();
    assert(consumer.succeeded());
    assert(items.done());
    assert(consumed == buffer.size());
    return 0;
}
