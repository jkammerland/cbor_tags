#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/codec/typed_array.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

using namespace cbor::tags;

/**
numbers = #6.78(bstr)
*/
int main() {
    // Hold the encoded data that would normally be transferred to the receiver.
    std::vector<std::byte> buffer;

    // Define signed 32-bit array elements.
    std::vector<std::int32_t> source{1, 2, 3};

    // Opt into the typed-array codec for little-endian elements.
    auto encoder = make_encoder<codec::typed_array>(buffer);
    if (!encoder(rfc8746::as_typed_array(source))) {
        return 1;
    }

    // Store decoded numbers in a typed array that owns its elements.
    rfc8746::typed_array<std::int32_t> received;

    // Decode the tagged byte string with the typed-array codec.
    auto decoder = make_decoder<codec::typed_array>(buffer);
    if (!decoder(received)) {
        return 1;
    }

    // Compare the numerical values rather than the byte-string representation.
    if (received.values() != source || decoder.tell() != buffer.end()) {
        return 2;
    }

    // Reuse application-owned vector storage with an explicit element limit.
    std::vector<std::int32_t> reused;
    reused.reserve(8);
    const auto *reused_storage = reused.data();

    // reserve() supplies capacity; the bound prevents growth past eight elements.
    auto reuse_decoder = make_decoder<codec::typed_array>(buffer);
    if (!reuse_decoder(as_bounded_size<0, 8>(rfc8746::as_typed_array(reused)))) {
        return 1;
    }

    if (reused != source || reused.data() != reused_storage ||
        reuse_decoder.tell() != buffer.end()) {
        return 2;
    }

    // Other compatible sequences can replace their contents without contiguous storage.
    std::deque<std::int32_t> sequence;
    auto sequence_decoder = make_decoder<codec::typed_array>(buffer);
    if (!sequence_decoder(rfc8746::as_typed_array(sequence))) {
        return 1;
    }

    if (!std::ranges::equal(sequence, source) || sequence_decoder.tell() != buffer.end()) {
        return 2;
    }

    // A span requires exactly its element count, including with runtime bounds.
    std::array<std::int32_t, 3> fixed{};
    auto span_decoder = make_decoder<codec::typed_array>(buffer);
    if (!span_decoder(
            as_bounded_size(rfc8746::as_typed_array(std::span{fixed}), 0, fixed.size()))) {
        return 1;
    }

    if (fixed != std::array<std::int32_t, 3>{1, 2, 3} || span_decoder.tell() != buffer.end()) {
        return 2;
    }

    // A dynamic-extent span also rejects a size mismatch before changing storage.
    std::array<std::int32_t, 2> too_short{9, 9};
    auto mismatch_decoder = make_decoder<codec::typed_array>(buffer);
    auto mismatch = mismatch_decoder(rfc8746::as_typed_array(std::span<std::int32_t>{too_short}));
    if (mismatch || mismatch.error() != status_code::unexpected_group_size ||
        too_short != std::array<std::int32_t, 2>{9, 9}) {
        return 2;
    }
    return 0;
}
