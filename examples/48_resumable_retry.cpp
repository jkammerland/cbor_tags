#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_resumable.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

namespace ct = cbor::tags;

// Delegate double decoding, then apply an application rule.
template <typename Self> struct finite_doubles : ct::codec::decoder_base<Self> {
    using ct::codec::decoder_base<Self>::decode;

    ct::resumable<ct::status_code> resumable_decode(double &value) {
        auto &decoder = static_cast<Self &>(*this);
        const auto status = co_await decoder.retry(value);
        if (status != ct::status_code::success)
            co_return status;
        co_return std::isfinite(value) ? ct::status_code::success : ct::status_code::error;
    }
};

/**
measurement = float
*/
int main() {
    const double sent = 1.25;
    std::vector<std::byte> wire;
    auto encoder = ct::make_encoder(wire);
    const auto encode_result = encoder(sent);
    assert(encode_result);
    assert(wire.size() > 2);

    // The hook awaits one retry operation while input arrives byte by byte.
    std::vector<std::byte> input;
    auto decoder = ct::make_resumable_decoder<finite_doubles>(input);
    double received{};
    auto operation = decoder(received);
    assert(!operation.done());

    // Every incomplete prefix causes another wait; the final byte completes the decode.
    for (const auto byte : wire) {
        input.push_back(byte);
        const auto state = operation.resume();
        if (input.size() < wire.size()) {
            assert(state == ct::resume_state::need_input);
            assert(operation.committed_bytes() == 0);
        } else {
            assert(state == ct::resume_state::done);
        }
    }

    const auto decode_result = operation.result();
    assert(decode_result);
    assert(received == sent);
    return 0;
}
