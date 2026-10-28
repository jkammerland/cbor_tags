#include "codec.h"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <nanobench.h>
#include <optional>
#include <utility>

namespace resumable_bench {

struct throughput_options {
    std::size_t bytes       = 16 * 1024 * 1024;
    std::size_t repetitions = 3;
    std::size_t epochs      = 9;
    std::string workload    = "all";
    std::string mode        = "all";
    bool        self_test   = false;
};

struct variant {
    std::string name;
    std::size_t chunk_size{};
};

std::vector<variant> variants(const throughput_options &options, std::size_t wire_bytes) {
    std::vector<variant> result;
    if (options.mode != "resumable") {
        result.push_back({"one-shot", 0});
    }
#ifndef CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY
    if (options.mode != "one-shot") {
        result.push_back({"resumable-full", wire_bytes});
        for (const auto chunk : {1460U, 4096U, 65536U}) {
            result.push_back({"resumable-" + std::to_string(chunk), chunk});
        }
    }
#endif
    require(!result.empty(), "resumable mode is unavailable in a one-shot-only build");
    return result;
}

template <typename T> bool decode_variant(const variant &mode, const bytes &encoded, T &destination) {
#ifndef CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY
    if (mode.chunk_size != 0) {
        return decode_resumable(encoded, destination, mode.chunk_size);
    }
#else
    (void)mode;
#endif
    return decode_one_shot(encoded, destination);
}

template <typename T> bool encode_variant(const variant &mode, const T &value, bytes &encoded) {
#ifndef CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY
    if (mode.chunk_size != 0) {
        return encode_resumable(value, encoded, mode.chunk_size);
    }
#else
    (void)mode;
#endif
    return encode_one_shot(value, encoded);
}

bool validate_variant(const variant &mode, const bytes &encoded) {
#ifndef CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY
    if (mode.chunk_size != 0) {
        return validate_resumable(encoded, mode.chunk_size);
    }
#else
    (void)mode;
#endif
    return validate_one_shot(encoded);
}

struct sample {
    std::string workload;
    std::string direction;
    std::string mode;
    double      ns_per_byte{};
};

void print_measurement(const std::string &workload, std::string_view direction, const variant &mode, std::size_t repetition,
                       std::size_t wire_bytes, const ankerl::nanobench::Result &result, std::vector<sample> &samples) {
    using measure      = ankerl::nanobench::Result::Measure;
    const auto seconds = result.median(measure::elapsed);
    const auto ns_byte = seconds * 1e9 / static_cast<double>(wire_bytes);
    samples.push_back({workload, std::string{direction}, mode.name, ns_byte});
    std::cout << workload << ',' << direction << ',' << mode.name << ',' << repetition << ',' << wire_bytes << ','
              << static_cast<double>(wire_bytes) / seconds / 1e6 << ',' << ns_byte << ','
              << result.minimum(measure::elapsed) * 1e9 / static_cast<double>(wire_bytes) << ','
              << result.maximum(measure::elapsed) * 1e9 / static_cast<double>(wire_bytes) << ',' << result.size() << ','
              << result.sum(measure::iterations) << '\n';
}

template <typename T>
void run_workload(const throughput_options &options, const std::string &name, const T &value, std::optional<std::size_t> exact_size,
                  std::vector<sample> &samples) {
    const auto encoded = encode_fixture(value, exact_size);
    check_fixture(value, encoded);
    auto  modes = variants(options, encoded.size());
    bytes output(encoded.size());

    // Verify each complete path before recording measurements, including every chunk size.
    for (const auto &mode : modes) {
        T decoded;
        require(decode_variant(mode, encoded, decoded) && decoded == value, "decode preflight mismatch");
        require(encode_variant(mode, value, output) && output == encoded, "encode preflight mismatch");
        require(validate_variant(mode, encoded), "validation preflight failed");
    }
    if (options.self_test) {
        std::cout << name << ": fixture and codec paths passed (" << encoded.size() << " bytes)\n";
        return;
    }

    for (std::size_t repetition = 0; repetition < options.repetitions; ++repetition) {
        // Reverse each successive round so ordinary/resumable paths do not always run first.
        if (repetition != 0) {
            std::reverse(modes.begin(), modes.end());
        }
        for (const auto &mode : modes) {
            ankerl::nanobench::Bench bench;
            bench.output(nullptr).unit("byte").batch(encoded.size()).performanceCounters(false);
            bench.epochs(options.epochs).minEpochTime(std::chrono::milliseconds{20}).minEpochIterations(1);

            std::optional<T> decoded;
            bool             succeeded = true;
            bench.run("decode", [&] {
                // All decode variants start with fresh destination storage, including its allocation cost.
                decoded.emplace();
                succeeded &= decode_variant(mode, encoded, *decoded);
                ankerl::nanobench::doNotOptimizeAway(*decoded);
                ankerl::nanobench::doNotOptimizeAway(succeeded);
            });
            require(succeeded && decoded && *decoded == value, "measured decode mismatch");
            print_measurement(name, "decode", mode, repetition + 1, encoded.size(), bench.results().back(), samples);

            succeeded = true;
            bench.run("encode", [&] {
                // Output storage is supplied before timing, equally for ordinary and resumable encoders.
                succeeded &= encode_variant(mode, value, output);
                ankerl::nanobench::doNotOptimizeAway(output.data());
                ankerl::nanobench::doNotOptimizeAway(succeeded);
            });
            require(succeeded && output == encoded, "measured encode mismatch");
            print_measurement(name, "encode", mode, repetition + 1, encoded.size(), bench.results().back(), samples);

            succeeded = true;
            bench.run("validate", [&] {
                succeeded &= validate_variant(mode, encoded);
                ankerl::nanobench::doNotOptimizeAway(succeeded);
            });
            require(succeeded, "measured validation failed");
            print_measurement(name, "validate", mode, repetition + 1, encoded.size(), bench.results().back(), samples);
        }
    }
}

void print_summaries(const std::vector<sample> &samples) {
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto &current  = samples[i];
        const auto  same_key = [&](const sample &other) {
            return current.workload == other.workload && current.direction == other.direction && current.mode == other.mode;
        };
        if (std::any_of(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(i), same_key)) {
            continue;
        }
        std::vector<double> values;
        for (const auto &other : samples) {
            if (same_key(other)) {
                values.push_back(other.ns_per_byte);
            }
        }
        std::sort(values.begin(), values.end());
        const auto median = (values[(values.size() - 1) / 2] + values[values.size() / 2]) / 2;
        std::cout << "summary," << current.workload << ',' << current.direction << ',' << current.mode << ",samples=" << values.size()
                  << ",median_ns/B=" << median << ",min_ns/B=" << values.front() << ",max_ns/B=" << values.back()
                  << ",median_MB/s=" << 1000 / median << '\n';
    }
}

throughput_options parse_options(int argc, char **argv) {
    throughput_options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument{argv[i]};
        if (argument == "--self-test") {
            options.self_test = true;
            continue;
        }
        if (argument == "--help") {
            std::cout << "--bytes N --repetitions N --epochs N --workload all|structured|bstr|tstr|scalars\n"
                         "--mode all|one-shot|resumable --self-test\n";
            std::exit(0);
        }
        require(i + 1 < argc, "missing option value");
        const std::string_view value{argv[++i]};
        if (argument == "--bytes") {
            options.bytes = parse_size(value);
        } else if (argument == "--repetitions") {
            options.repetitions = parse_size(value);
        } else if (argument == "--epochs") {
            options.epochs = parse_size(value);
        } else if (argument == "--workload") {
            options.workload = value;
        } else if (argument == "--mode") {
            options.mode = value;
        } else {
            throw std::runtime_error("unknown option: " + std::string{argument});
        }
    }
    require(options.workload == "all" || options.workload == "structured" || options.workload == "bstr" || options.workload == "tstr" ||
                options.workload == "scalars",
            "unknown workload");
    require(options.mode == "all" || options.mode == "one-shot" || options.mode == "resumable", "unknown mode");
    if (options.self_test) {
        options.bytes = 16384;
    }
    return options;
}

} // namespace resumable_bench

int main(int argc, char **argv) {
    using namespace resumable_bench;
    try {
        const auto          options = parse_options(argc, argv);
        std::vector<sample> samples;
        std::cout << std::fixed << std::setprecision(4);
        if (!options.self_test) {
            std::cout << "workload,direction,mode,repetition,encoded_bytes,MB/s,ns/B,epoch_min_ns/B,epoch_max_ns/B,epochs,iterations\n";
        }
        if (options.workload == "all" || options.workload == "structured") {
            run_workload(options, "structured", make_records(options.bytes), options.bytes, samples);
        }
        if (options.workload == "all" || options.workload == "bstr") {
            run_workload(options, "bstr", make_binary(options.bytes), options.bytes, samples);
        }
        if (options.workload == "all" || options.workload == "tstr") {
            run_workload(options, "tstr", make_text(options.bytes), options.bytes, samples);
        }
        if (options.workload == "all" || options.workload == "scalars") {
            run_workload(options, "scalars", make_scalars(options.bytes), {}, samples);
        }
        print_summaries(samples);
    } catch (const std::exception &error) {
        std::cerr << "benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
