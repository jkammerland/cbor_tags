#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_resumable.h"

#include <cassert>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace ct = cbor::tags;

namespace app {

struct Report {
    std::vector<int> samples;
    std::string station;
};

// ADL hooks choose a wire field order independently of the aggregate member order.
template <typename Encoder>
ct::resumable_task<ct::status_code> resumable_encode(Encoder &encoder, const Report &value) {
    co_return co_await encoder(ct::wrap_as_array{value.station, value.samples});
}

template <typename Decoder>
ct::resumable_task<ct::status_code> resumable_decode(Decoder &decoder, Report &value) {
    co_return co_await decoder(ct::wrap_as_array{value.station, value.samples});
}

} // namespace app

/**
report = [station: tstr, samples: [* int]]
*/
int main() {
    // The application value and output storage outlive the suspended encode hook.
    std::vector<std::byte> output;
    auto encoder = ct::make_resumable_encoder(output);
    const app::Report sent{{-1, 24, 1000}, "north"};
    auto encoding = encoder(sent);
    assert(encoding.done());

    // Check the selected field order independently through the ordinary array decoder.
    const auto encoded = encoding.result();
    assert(encoded);
    assert(*encoded == encoding.committed_bytes());
    auto produced = std::span<const std::byte>{output}.first(*encoded);
    auto decoder = ct::make_decoder(produced);
    std::string station;
    std::vector<int> samples;
    const auto fields = decoder(ct::wrap_as_array{station, samples});
    assert(fields);
    assert(station == sent.station);
    assert(samples == sent.samples);
    assert(decoder.tell() == produced.end());

    // Append received bytes to an ordinary container, then resume the hook.
    std::vector<std::byte> input;
    auto resumable_decoder = ct::make_resumable_decoder(input);
    app::Report received;
    auto decoding = resumable_decoder(received);
    input.insert(input.end(), produced.begin(), produced.begin() + 2);
    const auto first_input_state = decoding.resume();
    assert(first_input_state == ct::resume_state::need_input);

    input.insert(input.end(), produced.begin() + 2, produced.end());
    const auto final_input_state = decoding.resume();
    assert(final_input_state == ct::resume_state::finished);

    // The hook resumes inside its fields, then reconstructs the application layout.
    const auto decoded = decoding.result();
    assert(decoded);
    assert(*decoded == *encoded);
    assert(received.station == sent.station);
    assert(received.samples == sent.samples);
    return 0;
}
