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
    const auto encoded = ct::make_encoder(wire)(sent);
    assert(encoded);
    assert(wire.size() > 1);

    // The codec's input is incomplete until the second delivery arrives.
    std::vector<std::byte> input(wire.begin(), wire.end() - 1);
    auto decoder = ct::make_resumable_decoder<finite_doubles>(input);
    double received{};
    auto operation = decoder(received);
    assert(!operation.done());

    input.push_back(wire.back());
    const auto completed = operation.resume();
    assert(completed == ct::resume_state::done);
    assert(operation.result());
    assert(received == sent);
    return 0;
}
