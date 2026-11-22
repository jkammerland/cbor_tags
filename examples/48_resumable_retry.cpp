#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_resumable.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
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

    // The hook awaits one retry operation while input arrives byte by byte.
    std::vector<std::byte> input;
    auto decoder = ct::make_resumable_decoder<finite_doubles>(input);
    double received{};
    auto operation = decoder(received);
    assert(!operation.done());

    std::size_t next_byte{};
    for (;;) {
        input.push_back(wire.at(next_byte++));
        if (operation.resume() == ct::resume_state::done) {
            break;
        }
    }

    const auto decode_result = operation.result();
    assert(decode_result);
    assert(received == sent);
    std::cout << "decoded " << received << '\n';

    // An infinity is a valid double on the wire. The hook's own rule refuses it
    std::vector<std::byte> infinite_wire;
    auto infinite_encoder = ct::make_encoder(infinite_wire);
    const auto infinite_result = infinite_encoder(std::numeric_limits<double>::infinity());
    assert(infinite_result);

    auto rejecting_decoder = ct::make_resumable_decoder<finite_doubles>(infinite_wire);
    double refused{};
    auto rejection = rejecting_decoder(refused);
    assert(rejection.done());
    assert(!rejection.result());

    // The destination was written before the rule ran, so it holds the infinity
    std::cout << "refused " << refused << ": " << ct::status_message(rejection.result().error())
              << '\n';
    return 0;
}
