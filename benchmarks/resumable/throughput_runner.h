#pragma once

#include "fixtures.h"

#include <variant>

#if defined(_WIN32)
#if defined(CBOR_TAGS_ORDINARY_RUNNER_BUILD)
#define CBOR_TAGS_ORDINARY_RUNNER_API __declspec(dllexport)
#else
#define CBOR_TAGS_ORDINARY_RUNNER_API __declspec(dllimport)
#endif
#else
#define CBOR_TAGS_ORDINARY_RUNNER_API __attribute__((visibility("default")))
#endif

namespace resumable_bench {

struct throughput_options {
    std::size_t bytes              = 16 * 1024 * 1024;
    std::size_t repetitions        = 3;
    std::size_t epochs             = 9;
    std::size_t profile_iterations = 0;
    std::string workload           = "all";
    std::string mode               = "all";
    std::string storage            = "cold";
    std::string direction          = "all";
    std::string transport          = "all";
    std::size_t cbor_chunk         = 256;
    bool        allocations        = false;
    bool        self_test          = false;
};

struct variant {
    std::string name;
    std::size_t chunk_size{};
};

enum class wire_format { ordinary, indefinite, typed_native, typed_opposite, string_chunks };

enum class runner_phase { preflight, allocations, measurement };

struct sample {
    std::string workload;
    std::string direction;
    std::string mode;
    double      ns_per_byte{};
};

// References preserve the driver's single fixture and its lifetime across modes.
template <wire_format Format, typename T> struct fixture_ref {
    static constexpr wire_format format = Format;
    const T                     *value;
};

template <typename T> using ordinary_fixture   = fixture_ref<wire_format::ordinary, T>;
template <typename T> using indefinite_fixture = fixture_ref<wire_format::indefinite, T>;
using integer_map                              = std::map<std::uint32_t, std::uint32_t>;
using integer_unordered_map                    = std::unordered_map<std::uint32_t, std::uint32_t>;
using float_map                                = std::map<float, bool>;
using scalar_values                            = std::vector<std::uint32_t>;

// Unsupported fixture/format combinations fail at the driver's construction site.
using runner_input =
    std::variant<ordinary_fixture<records>, ordinary_fixture<bytes>, ordinary_fixture<std::string>, ordinary_fixture<scalar_values>,
                 ordinary_fixture<std::vector<float>>, ordinary_fixture<std::vector<double>>, ordinary_fixture<std::deque<bool>>,
                 ordinary_fixture<float_map>, ordinary_fixture<integer_map>, ordinary_fixture<integer_unordered_map>,
                 indefinite_fixture<scalar_values>, indefinite_fixture<std::vector<float>>, indefinite_fixture<std::vector<double>>,
                 indefinite_fixture<std::deque<bool>>, indefinite_fixture<float_map>, indefinite_fixture<integer_map>,
                 indefinite_fixture<integer_unordered_map>, fixture_ref<wire_format::typed_native, scalar_values>,
                 fixture_ref<wire_format::typed_opposite, scalar_values>, fixture_ref<wire_format::string_chunks, bytes>,
                 fixture_ref<wire_format::string_chunks, std::string>>;

CBOR_TAGS_ORDINARY_RUNNER_API bytes make_runner_fixture(const runner_input &input, const throughput_options &options,
                                                        std::optional<std::size_t> exact_size);

// Calls cross the runner boundary before any timed callback starts. A false
// result means this fixture has no allocation-counting destination.
CBOR_TAGS_ORDINARY_RUNNER_API bool run_ordinary_mode(const runner_input &input, const throughput_options &options, const std::string &name,
                                                     const bytes &encoded, bytes &output, const variant &mode, runner_phase phase,
                                                     std::size_t repetition, std::vector<sample> &samples);
bool run_resumable_mode(const runner_input &input, const throughput_options &options, const std::string &name, const bytes &encoded,
                        bytes &output, const variant &mode, runner_phase phase, std::size_t repetition, std::vector<sample> &samples);

} // namespace resumable_bench

#undef CBOR_TAGS_ORDINARY_RUNNER_API
