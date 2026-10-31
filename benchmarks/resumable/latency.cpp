#include "codec.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>

namespace resumable_bench {

using clock_type = std::chrono::steady_clock;
using seconds    = std::chrono::duration<double>;

struct latency_options {
    std::size_t bytes       = 1'000'000'000;
    std::size_t rate        = 100'000'000;
    std::size_t chunk       = 65'536;
    std::size_t repetitions = 3;
    std::string workload    = "all";
    std::string mode        = "all";
    bool        self_test   = false;
};

// Only this independent publisher admits input. Decoder work never advances its deadlines.
class paced_input {
  public:
    paced_input(const bytes &encoded, std::size_t chunk_size, std::size_t bits_per_second) {
        for (std::size_t offset = 0; offset < encoded.size();) {
            const auto size = std::min(chunk_size, encoded.size() - offset);
            chunks.push_back(std::span<const std::byte>{encoded}.subspan(offset, size));
            offset += size;
            const auto delay = seconds{static_cast<double>(offset) * 8.0 / static_cast<double>(bits_per_second)};
            require(delay < seconds{clock_type::duration::max()} / 2, "requested schedule exceeds clock duration range");
            deadlines.push_back(std::chrono::duration_cast<clock_type::duration>(delay));
        }
        arrivals.resize(chunks.size());
        publisher = std::jthread([this](std::stop_token stop) { publish(stop); });
    }

    ~paced_input() {
        publisher.request_stop();
        changed.notify_all();
        publisher.join();
    }

    void start(clock_type::time_point epoch) {
        {
            const std::lock_guard lock{mutex};
            start_time = epoch;
            started    = true;
        }
        changed.notify_all();
    }

    std::size_t wait_after(std::size_t consumed) {
        std::unique_lock lock{mutex};
        changed.wait(lock, [&] { return published > consumed; });
        return published;
    }

    void wait_all() {
        std::unique_lock lock{mutex};
        changed.wait(lock, [&] { return published == chunks.size(); });
    }

    std::vector<std::span<const std::byte>> chunks;
    std::vector<clock_type::duration>       deadlines;
    std::vector<clock_type::time_point>     arrivals;

  private:
    void publish(std::stop_token stop) {
        {
            std::unique_lock lock{mutex};
            changed.wait(lock, [&] { return started || stop.stop_requested(); });
        }
        for (std::size_t i = 0; i < chunks.size() && !stop.stop_requested(); ++i) {
            std::unique_lock lock{mutex};
            changed.wait_until(lock, start_time + deadlines[i], [&] { return stop.stop_requested(); });
            if (stop.stop_requested()) {
                return;
            }
            arrivals[i] = clock_type::now();
            published   = i + 1;
            lock.unlock();
            changed.notify_all();
        }
    }

    std::mutex              mutex;
    std::condition_variable changed;
    clock_type::time_point  start_time{};
    bool                    started{};
    std::size_t             published{};
    std::jthread            publisher;
};

struct latency_sample {
    std::string workload;
    std::string mode;
    std::size_t repetition{};
    double      total_seconds{};
    double      last_arrival_seconds{};
    double      tail_seconds{};
    double      maximum_lateness_seconds{};
    double      mean_lateness_seconds{};
    double      final_lateness_seconds{};
};

template <typename T>
latency_sample run_trial(const latency_options &options, const std::string &workload, const T &value, const bytes &encoded, bool resumable,
                         std::size_t repetition) {
    T                      destination;
    paced_input            input{encoded, options.chunk, options.rate};
    clock_type::time_point start;
    clock_type::time_point completed;
    bool                   succeeded{};

#ifndef CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY
    if (resumable) {
        std::list<std::span<const std::byte>> descriptors;
#ifdef CBOR_TAGS_BENCHMARK_USE_STORAGE_FACTORY
        auto decoder = ct::make_resumable_decoder(descriptors);
#else
        auto source  = ct::make_input_source(descriptors);
        auto decoder = ct::make_resumable_decoder(source);
#endif
        auto operation = decoder(destination);

        start = clock_type::now();
        input.start(start);
        for (std::size_t consumed = 0; consumed < input.chunks.size();) {
            const auto available = input.wait_after(consumed);
            operation(std::span{input.chunks}.subspan(consumed, available - consumed));
            consumed = available;
        }
        completed         = clock_type::now();
        const auto result = operation.result();
        succeeded         = operation.done() && result && *result == encoded.size();
    } else
#endif
    {
        require(!resumable, "resumable mode is unavailable in a one-shot-only build");
        start = clock_type::now();
        input.start(start);
        input.wait_all();
        succeeded = decode_one_shot(encoded, destination);
        completed = clock_type::now();
    }

    // Full equality and accounting checks are deliberately outside the completion interval.
    require(succeeded && destination == value, "paced decode mismatch");
    require(input.arrivals.back() <= completed, "decoder completed before the final admitted bytes");
    latency_sample sample{workload, resumable ? "resumable" : "one-shot", repetition};
    sample.total_seconds        = seconds{completed - start}.count();
    sample.last_arrival_seconds = seconds{input.arrivals.back() - start}.count();
    sample.tail_seconds         = seconds{completed - input.arrivals.back()}.count();
    for (std::size_t i = 0; i < input.arrivals.size(); ++i) {
        const auto late = seconds{input.arrivals[i] - start - input.deadlines[i]}.count();
        require(late >= 0, "publisher admitted input before its deadline");
        sample.maximum_lateness_seconds = std::max(sample.maximum_lateness_seconds, late);
        sample.mean_lateness_seconds += late;
        sample.final_lateness_seconds = late;
    }
    sample.mean_lateness_seconds /= static_cast<double>(input.arrivals.size());
    return sample;
}

void print_sample(const latency_sample &sample) {
    std::cout << sample.workload << ',' << sample.mode << ',' << sample.repetition << ',' << sample.total_seconds << ','
              << sample.last_arrival_seconds << ',' << sample.tail_seconds << ',' << sample.maximum_lateness_seconds << ','
              << sample.mean_lateness_seconds << ',' << sample.final_lateness_seconds << std::endl;
}

template <typename T>
void run_workload(const latency_options &options, const std::string &name, const T &value, std::vector<latency_sample> &samples) {
    const auto encoded = encode_fixture(value, options.bytes);
    check_fixture(value, encoded);

    std::vector<bool> modes;
    if (options.mode != "resumable") {
        modes.push_back(false);
    }
#ifndef CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY
    if (options.mode != "one-shot") {
        modes.push_back(true);
    }
#endif
    require(!modes.empty(), "resumable mode is unavailable in a one-shot-only build");
    for (std::size_t repetition = 0; repetition < options.repetitions; ++repetition) {
        if (repetition != 0) {
            std::reverse(modes.begin(), modes.end());
        }
        for (const auto resumable : modes) {
            samples.push_back(run_trial(options, name, value, encoded, resumable, repetition + 1));
            print_sample(samples.back());
        }
    }
}

void print_summaries(const std::vector<latency_sample> &samples) {
    constexpr std::array names{"total_s", "last_arrival_s", "final_byte_tail_s", "max_lateness_s", "mean_lateness_s", "final_lateness_s"};
    constexpr std::array members{&latency_sample::total_seconds,         &latency_sample::last_arrival_seconds,
                                 &latency_sample::tail_seconds,          &latency_sample::maximum_lateness_seconds,
                                 &latency_sample::mean_lateness_seconds, &latency_sample::final_lateness_seconds};
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto &current = samples[i];
        const auto same_key = [&](const latency_sample &other) { return current.workload == other.workload && current.mode == other.mode; };
        if (std::any_of(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(i), same_key)) {
            continue;
        }
        for (std::size_t metric = 0; metric < names.size(); ++metric) {
            std::vector<double> values;
            for (const auto &other : samples) {
                if (same_key(other)) {
                    values.push_back(other.*members[metric]);
                }
            }
            std::sort(values.begin(), values.end());
            const auto median = (values[(values.size() - 1) / 2] + values[values.size() / 2]) / 2;
            std::cout << "summary," << current.workload << ',' << current.mode << ',' << names[metric] << ",samples=" << values.size()
                      << ",median=" << median << ",min=" << values.front() << ",max=" << values.back() << '\n';
        }
    }
}

latency_options parse_options(int argc, char **argv) {
    latency_options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument{argv[i]};
        if (argument == "--self-test") {
            options.self_test = true;
            continue;
        }
        if (argument == "--help") {
            std::cout << "--bytes N --rate BITS_PER_SECOND --chunk BYTES --repetitions N\n"
                         "--workload all|structured|bstr --mode all|one-shot|resumable --self-test\n"
                         "Defaults: exactly 1,000,000,000 encoded bytes, 100,000,000 bit/s, 65,536 byte chunks, 3 pairs per workload.\n";
            std::exit(0);
        }
        require(i + 1 < argc, "missing option value");
        const std::string_view value{argv[++i]};
        if (argument == "--bytes") {
            options.bytes = parse_size(value);
        } else if (argument == "--rate") {
            options.rate = parse_size(value);
        } else if (argument == "--chunk") {
            options.chunk = parse_size(value);
        } else if (argument == "--repetitions") {
            options.repetitions = parse_size(value);
        } else if (argument == "--workload") {
            options.workload = value;
        } else if (argument == "--mode") {
            options.mode = value;
        } else {
            throw std::runtime_error("unknown option: " + std::string{argument});
        }
    }
    require(options.workload == "all" || options.workload == "structured" || options.workload == "bstr", "unknown workload");
    require(options.mode == "all" || options.mode == "one-shot" || options.mode == "resumable", "unknown mode");
    if (options.self_test) {
        options.bytes       = 16384;
        options.rate        = 100'000'000;
        options.chunk       = 257;
        options.repetitions = 1;
    }
    return options;
}

} // namespace resumable_bench

int main(int argc, char **argv) {
    using namespace resumable_bench;
    try {
        const auto options = parse_options(argc, argv);
        std::cout << std::fixed << std::setprecision(9);
        std::cout << "in-process timed arrival: encoded_bytes=" << options.bytes << ",rate_bits/s=" << options.rate
                  << ",chunk_bytes=" << options.chunk
                  << ",nominal_seconds=" << static_cast<double>(options.bytes) * 8.0 / static_cast<double>(options.rate)
                  << ",repetitions=" << options.repetitions << std::endl;
        std::cout << "workload,mode,repetition,total_s,last_arrival_s,final_byte_tail_s,max_lateness_s,mean_lateness_s,final_lateness_s\n";
        std::vector<latency_sample> samples;
        if (options.workload == "all" || options.workload == "structured") {
            run_workload(options, "structured", make_records(options.bytes), samples);
        }
        if (options.workload == "all" || options.workload == "bstr") {
            run_workload(options, "bstr", make_binary(options.bytes), samples);
        }
        print_summaries(samples);
    } catch (const std::exception &error) {
        std::cerr << "benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
