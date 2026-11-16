#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_resumable.h"
#include "cbor_tags/codec/typed_array.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <deque>
#include <span>
#include <vector>

namespace ct = cbor::tags;

/**
samples = #6.85(bstr)
*/
int main() {
    // The sender has 4 float samples. As a typed array that's tag 85 and a 16 byte string
    const std::vector<float> sent{0.5F, -1.25F, 20.0F, 1000.0F};
    std::vector<std::byte> buffer;
    auto encoder = ct::make_encoder<ct::codec::typed_array>(buffer);
    const auto encoded = encoder(ct::rfc8746::as_typed_array(sent));
    assert(encoded);
    assert(buffer.size() == 19); // d8 55 50, then the floats

    // The receiver reuses its own vector and takes at most 8 samples
    std::vector<float> samples;
    samples.reserve(8);
    const auto *storage = samples.data();

    std::deque<std::span<const std::byte>> input;
    auto decoder = ct::make_resumable_decoder<ct::codec::typed_array>(input);
    auto operation = decoder(ct::as_bounded_size<0, 8>(ct::rfc8746::as_typed_array(samples)));

    // The first chunk ends inside the second float. Floats are taken whole, so only one is in
    const auto bytes = std::span<const std::byte>{buffer};
    input.push_back(bytes.first(9));
    const auto first_state = operation.resume();
    assert(first_state == ct::resume_state::need_input);
    assert(samples.size() == 1);
    assert(samples[0] == 0.5F);

    // The rest of the bytes finish it
    input.push_back(bytes.subspan(9));
    const auto final_state = operation.resume();
    assert(final_state == ct::resume_state::done);
    const auto result = operation.result();
    assert(result);
    assert(*result == buffer.size());

    // Same values, same storage. reserve() made room and the bound keeps it from needing more
    assert(samples == sent);
    assert(samples.data() == storage);

    // A receiver that takes at most 2 samples gives up on the header, before any float arrives.
    // Its vector isn't touched
    std::deque<std::span<const std::byte>> header_only{bytes.first(3)};
    std::vector<float> few{9.0F};
    auto small_decoder = ct::make_resumable_decoder<ct::codec::typed_array>(header_only);
    auto rejected = small_decoder(ct::as_bounded_size<0, 2>(ct::rfc8746::as_typed_array(few)));
    assert(rejected.done());
    assert(!rejected.result());
    assert(rejected.result().error() == ct::status_code::size_limit_exceeded);
    assert(few == std::vector<float>{9.0F});

    // A view borrows the floats from the input instead of copying them, so the whole array has to
    // be in the input at once. Name its type through the decoder
    std::vector<std::byte> received;
    auto view_decoder = ct::make_resumable_decoder<ct::codec::typed_array>(received);
    ct::rfc8746::typed_array_view_for<float, decltype(view_decoder)> view;
    {
        auto viewing = view_decoder(view);

        // The first chunk isn't enough. Nothing commits until the whole array is there
        received.insert(received.end(), buffer.begin(), buffer.begin() + 9);
        const auto partial_state = viewing.resume();
        assert(partial_state == ct::resume_state::need_input);
        assert(viewing.committed_bytes() == 0);

        received.insert(received.end(), buffer.begin() + 9, buffer.end());
        const auto view_state = viewing.resume();
        assert(view_state == ct::resume_state::done);
        assert(viewing.result());
    }

    // The view points into 'received', not into the operation. So it outlives the operation, and
    // still works after 'received' reallocates
    received.reserve(received.capacity() * 2);
    assert(view.size() == sent.size());
    assert(std::ranges::equal(view.values(), sent));
    return 0;
}
