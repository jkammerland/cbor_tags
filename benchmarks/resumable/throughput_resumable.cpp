#include "throughput_runner_impl.h"

namespace resumable_bench {

bool run_resumable_mode(const runner_input &input, const throughput_options &options, const std::string &name, const bytes &encoded,
                        bytes &output, const variant &mode, runner_phase phase, std::size_t repetition, std::vector<sample> &samples) {
    return std::visit(
        [&](const auto &fixture) {
            return run_mode<true, std::remove_cvref_t<decltype(fixture)>::format>(options, name, *fixture.value, encoded, output, mode,
                                                                                  phase, repetition, samples);
        },
        input);
}

} // namespace resumable_bench
