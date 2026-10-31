#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_resumable.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <list>
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
ct::resumable<ct::status_code> resumable_encode(Encoder &encoder, const Report &value) {
    co_return co_await encoder(ct::wrap_as_array{value.station, value.samples});
}

template <typename Decoder>
ct::resumable<ct::status_code> resumable_decode(Decoder &decoder, Report &value) {
    co_return co_await decoder(ct::wrap_as_array{value.station, value.samples});
}

} // namespace app

/**
report = [station: tstr, samples: [* int]]
*/
int main() {
    // The application value and output storage outlive the suspended encode hook.
    const app::Report sent{{-1, 24, 1000}, "north"};
    std::array<std::byte, 2> first{};
    std::array<std::byte, 64> second{};
    std::list<std::span<std::byte>> output_chunks;
    auto sink = ct::make_output_source(output_chunks);
    auto encoding = ct::make_resumable_encoder(sink)(sent);
    const auto first_output_state = encoding(first);
    assert(first_output_state == ct::resume_state::need_output);

    const auto final_output_state = encoding(second);
    assert(final_output_state == ct::resume_state::done);

    // Check the selected field order independently through the ordinary array decoder.
    const auto encoded = encoding.result();
    assert(encoded);
    assert(*encoded > first.size());
    assert(*encoded <= first.size() + second.size());
    auto produced = encoding.produced();
    std::string station;
    std::vector<int> samples;
    auto decoder = ct::make_decoder(produced);
    const auto fields = decoder(ct::wrap_as_array{station, samples});
    assert(fields);
    assert(station == sent.station);
    assert(samples == sent.samples);
    assert(decoder.tell() == produced.end());

    // Supply only written bytes; both backing buffers remain alive during decoding.
    std::list<std::span<const std::byte>> input_chunks;
    auto source = ct::make_input_source(input_chunks);
    app::Report received;
    auto decoding = ct::make_resumable_decoder(source)(received);
    const auto first_input_state = decoding(std::span<const std::byte>{first});
    assert(first_input_state == ct::resume_state::need_input);

    const auto final_input_state =
        decoding(std::span<const std::byte>{second}.first(*encoded - first.size()));
    assert(final_input_state == ct::resume_state::done);

    // The hook resumes inside its fields, then reconstructs the application layout.
    const auto decoded = decoding.result();
    assert(decoded);
    assert(*decoded == *encoded);
    assert(received.station == sent.station);
    assert(received.samples == sent.samples);
    return 0;
}
