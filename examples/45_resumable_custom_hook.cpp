#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_resumable.h"

#include <array>
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
    if (encoding(first) != ct::resume_state::need_output ||
        encoding(second) != ct::resume_state::done) {
        return 1;
    }

    // Check the selected field order independently through the ordinary array decoder.
    const auto encoded = encoding.result();
    if (!encoded || *encoded <= first.size() || *encoded > first.size() + second.size()) {
        return 2;
    }
    auto produced = encoding.produced();
    std::string station;
    std::vector<int> samples;
    auto decoder = ct::make_decoder(produced);
    if (!decoder(ct::wrap_as_array{station, samples}) || station != sent.station ||
        samples != sent.samples || decoder.tell() != produced.end()) {
        return 2;
    }

    // Supply only written bytes; both backing buffers remain alive during decoding.
    std::list<std::span<const std::byte>> input_chunks;
    auto source = ct::make_input_source(input_chunks);
    app::Report received;
    auto decoding = ct::make_resumable_decoder(source)(received);
    if (decoding(std::span<const std::byte>{first}) != ct::resume_state::need_input) {
        return 3;
    }
    if (decoding(std::span<const std::byte>{second}.first(*encoded - first.size()),
            ct::end_of_input) != ct::resume_state::done) {
        return 3;
    }

    // The hook resumes inside its fields, then reconstructs the application layout.
    const auto decoded = decoding.result();
    if (!decoded || *decoded != *encoded || received.station != sent.station ||
        received.samples != sent.samples) {
        return 4;
    }
    return 0;
}
