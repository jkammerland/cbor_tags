#include "fixtures.h"
#include "resumable_ring_fixture.h"

#include <cbor_tags/cbor_resumable.h>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <list>
#include <nanobench.h>
#include <ranges>
#include <type_traits>

namespace resumable_bench {

constexpr std::size_t packet_bytes = 1460;
constexpr std::size_t ring_offset  = 137;

enum class storage_case { vector, deque, span, vector_vector, deque_span, list_span, list_deque, vector_list, list_list, ring };

constexpr std::array case_names{"vector",    "deque",      "span",        "vector-vector", "deque-span",
                                "list-span", "list-deque", "vector-list", "list-list",     "ring"};

struct storage_options {
    std::size_t bytes              = 65536;
    std::size_t repetitions        = 3;
    std::size_t epochs             = 9;
    std::size_t profile_iterations = 0;
    std::string selected_case      = "all";
    std::string workload           = "all";
    std::string direction          = "all";
    std::string admission          = "all";
    bool        self_test          = false;
};

template <typename Storage>
concept reservable_storage = requires(Storage &storage, std::size_t size) { storage.reserve(size); };

template <storage_case Case> inline constexpr bool flat_storage     = Case <= storage_case::span;
template <storage_case Case> inline constexpr bool growable_output  = Case == storage_case::vector || Case == storage_case::deque;
template <storage_case Case> inline constexpr bool borrowed_packets = Case == storage_case::deque_span || Case == storage_case::list_span;

template <storage_case Case, bool Encode> struct storage_type;
template <bool Encode> struct storage_type<storage_case::vector, Encode> {
    using type = bytes;
};
template <bool Encode> struct storage_type<storage_case::deque, Encode> {
    using type = std::deque<std::byte>;
};
template <bool Encode> struct storage_type<storage_case::span, Encode> {
    using type = std::span<std::conditional_t<Encode, std::byte, const std::byte>>;
};
template <bool Encode> struct storage_type<storage_case::vector_vector, Encode> {
    using type = std::vector<bytes>;
};
template <bool Encode> struct storage_type<storage_case::deque_span, Encode> {
    using type = std::deque<typename storage_type<storage_case::span, Encode>::type>;
};
template <bool Encode> struct storage_type<storage_case::list_span, Encode> {
    using type = std::list<typename storage_type<storage_case::span, Encode>::type>;
};
template <bool Encode> struct storage_type<storage_case::list_deque, Encode> {
    using type = std::list<std::deque<std::byte>>;
};
template <bool Encode> struct storage_type<storage_case::vector_list, Encode> {
    using type = std::vector<std::list<std::byte>>;
};
template <bool Encode> struct storage_type<storage_case::list_list, Encode> {
    using type = std::list<std::list<std::byte>>;
};

// Persistent capacity and fixed output backing are prepared before measurement.
// Clearing storage, packet allocation, and input copying are part of each operation.
template <storage_case Case, bool Encode> struct storage_fixture {
    using storage_t = typename storage_type<Case, Encode>::type;
    storage_t storage;
    bytes     backing;

    storage_fixture(std::size_t size, std::size_t chunk) {
        if constexpr (reservable_storage<storage_t>) {
            // Self-tests admit two empty descriptors and one data descriptor per packet.
            storage.reserve(flat_storage<Case> ? size : 3 * (size / chunk + 1) + 1);
        }
        if constexpr (Encode && (Case == storage_case::span || borrowed_packets<Case>)) {
            backing.resize(size);
        }
    }

    void reset() {
        if constexpr (Case == storage_case::span) {
            storage = {};
        } else {
            storage.clear();
        }
    }

    void admit(const bytes &wire, std::size_t offset, std::size_t count) {
        if constexpr (Case == storage_case::span) {
            if constexpr (Encode) {
                storage = std::span<std::byte>{backing}.first(offset + count);
            } else {
                storage = std::span<const std::byte>{wire}.first(offset + count);
            }
        } else if constexpr (flat_storage<Case>) {
            if constexpr (!Encode) {
                storage.insert(storage.end(), wire.begin() + static_cast<std::ptrdiff_t>(offset),
                               wire.begin() + static_cast<std::ptrdiff_t>(offset + count));
            }
        } else if constexpr (borrowed_packets<Case>) {
            if constexpr (Encode) {
                storage.emplace_back(std::span<std::byte>{backing}.subspan(offset, count));
            } else {
                storage.emplace_back(std::span<const std::byte>{wire}.subspan(offset, count));
            }
        } else if constexpr (Encode) {
            storage.emplace_back(count);
        } else {
            storage.emplace_back(wire.begin() + static_cast<std::ptrdiff_t>(offset),
                                 wire.begin() + static_cast<std::ptrdiff_t>(offset + count));
        }
    }

    bool matches(const bytes &wire) const {
        if constexpr (flat_storage<Case>) {
            return std::ranges::equal(storage, wire);
        } else {
            return std::ranges::equal(storage | std::views::join, wire);
        }
    }
};

template <typename Operation> bool completed(Operation &operation, std::size_t size, bool self_test) {
    const auto result = operation.result();
    const bool valid  = operation.done() && result && *result == size && operation.committed_bytes() == size;
    if (self_test) {
        const auto repeated = operation.resume();
        require(repeated == ct::resume_state::finished && operation.committed_bytes() == size, "terminal resume changed completion");
    }
    return valid;
}

template <storage_case Case, bool Encode, typename T>
bool run_storage(storage_fixture<Case, Encode> &fixture, const T &value, const bytes &wire, T &decoded, bool staged, std::size_t chunk,
                 bool self_test) {
    fixture.reset();
    decoded.clear();

    // Both nested modes use the same packet boundaries; complete admits them all first.
    const auto admit = [&](std::size_t offset, std::size_t count) {
        if (self_test && !flat_storage<Case>) {
            fixture.admit(wire, offset, 0);
        }
        fixture.admit(wire, offset, count);
    };
    if (!staged && !(Encode && growable_output<Case>)) {
        if constexpr (flat_storage<Case>) {
            admit(0, wire.size());
        } else {
            for (std::size_t offset = 0; offset < wire.size(); offset += chunk) {
                admit(offset, std::min(chunk, wire.size() - offset));
            }
        }
    }

    auto factory = [&] {
        if constexpr (Encode) {
            return ct::make_resumable_encoder(fixture.storage);
        } else {
            return ct::make_resumable_decoder(fixture.storage);
        }
    }();
    auto operation = [&] {
        if constexpr (Encode) {
            return factory(value);
        } else {
            return factory(decoded);
        }
    }();

    if (staged) {
        for (std::size_t offset = 0; offset < wire.size(); offset += chunk) {
            if (self_test) {
                const auto before = operation.committed_bytes();
                fixture.admit(wire, offset, 0);
                const auto pending = operation.resume();
                require(pending == (Encode ? ct::resume_state::need_output : ct::resume_state::need_input),
                        "empty admission did not remain pending");
                require(operation.committed_bytes() == before, "empty admission changed commitment");
            }
            admit(offset, std::min(chunk, wire.size() - offset));
            operation.resume();
            if (self_test) {
                require(operation.committed_bytes() <= std::min(offset + chunk, wire.size()), "commitment exceeds admitted bytes");
            }
        }
    }
    return completed(operation, wire.size(), self_test);
}

// This public-protocol control does not instantiate the library's storage adapter.
// Only committed input is reclaimed; only committed output is copied to the collector.
template <bool Encode, typename T>
bool run_ring(const T &value, const bytes &wire, T &decoded, bytes &collected, std::size_t chunk, bool self_test) {
    resumable_test::ring_source<Encode, packet_bytes> source{ring_offset};
    decoded.clear();
    collected.clear();

    if constexpr (Encode) {
        auto encoder   = ct::make_resumable_encoder(source);
        auto operation = encoder(value);
        for (;;) {
            const auto committed = source.committed_position();
            const auto count     = committed.offset() - source.begin().offset();
            collected.insert(collected.end(), source.begin(), committed);
            source.reclaim(committed.offset());
            if (operation.done()) {
                break;
            }
            require(count != 0, "ring output made no committed progress");
            operation.resume();
        }
        require(source.committed_position().offset() == ring_offset + operation.committed_bytes(), "ring output offset mismatch");
        return completed(operation, wire.size(), self_test);
    } else {
        auto decoder   = ct::make_resumable_decoder(source);
        auto operation = decoder(decoded);
        for (std::size_t offset = 0; offset < wire.size();) {
            const auto count = source.append(std::span<const std::byte>{wire}.subspan(offset, std::min(chunk, wire.size() - offset)));
            require(count != 0, "ring input made no admission progress");
            offset += count;
            operation.resume();
            require(source.committed_position().offset() == ring_offset + operation.committed_bytes(), "ring input offset mismatch");
            source.reclaim(source.committed_position().offset());
        }
        require(source.size() == 0, "ring retained a completed input prefix");
        return completed(operation, wire.size(), self_test);
    }
}

struct storage_sample {
    std::string key;
    double      ns_per_byte{};
};

void print_storage_measurement(const std::string &key, std::string_view scope, std::size_t repetition, std::size_t size,
                               const ankerl::nanobench::Result &result, std::vector<storage_sample> &samples) {
    using measure   = ankerl::nanobench::Result::Measure;
    const auto ns_b = result.median(measure::elapsed) * 1e9 / static_cast<double>(size);
    samples.push_back({key, ns_b});
    std::cout << key << ',' << scope << ',' << repetition << ',' << size << ',' << 1000 / ns_b << ',' << ns_b << ','
              << result.minimum(measure::elapsed) * 1e9 / static_cast<double>(size) << ','
              << result.maximum(measure::elapsed) * 1e9 / static_cast<double>(size) << ',' << result.size() << ','
              << result.sum(measure::iterations) << '\n';
}

template <storage_case Case, bool Encode> std::string_view timed_storage_scope() {
    if constexpr (Case == storage_case::ring) {
        return Encode ? "ring-write+publish-copy" : "ring-admission-copy";
    } else if constexpr (Case == storage_case::span) {
        return "reserved-backing";
    } else if constexpr (borrowed_packets<Case>) {
        return "descriptor-allocation+reserved-backing";
    } else if constexpr (!flat_storage<Case>) {
        return Encode ? "owning-packet-allocation" : "owning-packet-copy+allocation";
    } else if constexpr (Case == storage_case::deque) {
        return Encode ? "append+deque-allocation" : "admission-copy+deque-allocation";
    } else {
        return Encode ? "reserved-append" : "admission-copy+reserved-storage";
    }
}

template <storage_case Case, bool Encode, typename T>
void run_case(const storage_options &options, std::string_view workload, const T &value, const bytes &wire, std::size_t chunk,
              std::vector<storage_sample> &samples) {
    const std::string_view case_name = case_names[static_cast<std::size_t>(Case)];
    if (options.selected_case != "all" && options.selected_case != case_name) {
        return;
    }
    if (options.direction != "all" && options.direction != (Encode ? "encode" : "decode")) {
        return;
    }

    T decoded;
    decoded.reserve(value.size());
    bytes collected;
    collected.reserve(wire.size());
    auto fixture = [&] {
        if constexpr (Case == storage_case::ring) {
            return 0;
        } else {
            return storage_fixture<Case, Encode>{wire.size(), chunk};
        }
    }();

    for (const bool staged : {false, true}) {
        if constexpr (Case == storage_case::ring) {
            if (!staged) {
                continue;
            }
        } else if constexpr (Encode && growable_output<Case>) {
            if (staged) {
                continue;
            }
        }
        if (options.admission != "all" && options.admission != (staged ? "1460" : "complete")) {
            continue;
        }

        const std::string admission = staged ? "staged" + std::to_string(chunk) : "complete";
        const std::string key =
            std::string{workload} + ',' + (Encode ? "encode" : "decode") + ',' + std::string{case_name} + ',' + admission;
        const auto run = [&] {
            if constexpr (Case == storage_case::ring) {
                return run_ring<Encode>(value, wire, decoded, collected, chunk, options.self_test);
            } else {
                return run_storage(fixture, value, wire, decoded, staged, chunk, options.self_test);
            }
        };
        const auto matches = [&] {
            if constexpr (!Encode) {
                return decoded == value;
            } else if constexpr (Case == storage_case::ring) {
                return collected == wire;
            } else {
                return fixture.matches(wire);
            }
        };

        // A prior mode must not supply bytes that a broken fixed-output path omits.
        if constexpr (Encode && Case != storage_case::ring) {
            for (std::size_t index = 0; index < fixture.backing.size(); ++index) {
                fixture.backing[index] = ~wire[index];
            }
        }
        const auto preflight = run();
        require(preflight && matches(), "storage preflight mismatch: " + key);
        if (options.self_test) {
            continue;
        }

        for (std::size_t repetition = 1; repetition <= options.repetitions; ++repetition) {
            ankerl::nanobench::Bench bench;
            bench.output(nullptr).unit("byte").batch(wire.size()).performanceCounters(false);
            bench.epochs(options.epochs).minEpochTime(std::chrono::milliseconds{20}).minEpochIterations(1);
            if (options.profile_iterations != 0) {
                bench.epochs(1).warmup(0).epochIterations(options.profile_iterations);
            }
            bool succeeded = true;
            bench.run(key, [&] {
                succeeded &= run();
                ankerl::nanobench::doNotOptimizeAway(succeeded);
            });
            require(succeeded && matches(), "measured storage mismatch: " + key);
            print_storage_measurement(key, timed_storage_scope<Case, Encode>(), repetition, wire.size(), bench.results().back(), samples);
        }
    }
}

template <storage_case Case, typename T>
void run_directions(const storage_options &options, std::string_view workload, const T &value, const bytes &wire, std::size_t chunk,
                    std::vector<storage_sample> &samples) {
    run_case<Case, false>(options, workload, value, wire, chunk, samples);
    run_case<Case, true>(options, workload, value, wire, chunk, samples);
}

template <typename T>
void run_storage_workload(const storage_options &options, std::string_view workload, const T &value, const bytes &wire, std::size_t chunk,
                          std::vector<storage_sample> &samples) {
    check_fixture(value, wire);
    run_directions<storage_case::vector>(options, workload, value, wire, chunk, samples);
    run_directions<storage_case::deque>(options, workload, value, wire, chunk, samples);
    run_directions<storage_case::span>(options, workload, value, wire, chunk, samples);
    run_directions<storage_case::vector_vector>(options, workload, value, wire, chunk, samples);
    run_directions<storage_case::deque_span>(options, workload, value, wire, chunk, samples);
    run_directions<storage_case::list_span>(options, workload, value, wire, chunk, samples);
    run_directions<storage_case::list_deque>(options, workload, value, wire, chunk, samples);
    run_directions<storage_case::vector_list>(options, workload, value, wire, chunk, samples);
    run_directions<storage_case::list_list>(options, workload, value, wire, chunk, samples);
    run_directions<storage_case::ring>(options, workload, value, wire, chunk, samples);
}

// Include all unsigned CBOR widths, including the eight-byte integer payload.
std::vector<std::uint64_t> storage_scalars(std::size_t budget) {
    constexpr std::array<std::uint64_t, 10> pattern{
        0, 23, 24, 255, 256, 65535, 65536, 0xffffffffULL, 0x100000000ULL, 0xffffffffffffffffULL};
    std::vector<std::uint64_t> result(std::max<std::size_t>(pattern.size(), budget / 4));
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = pattern[index % pattern.size()];
    }
    return result;
}

void run_fixtures(const storage_options &options, std::size_t budget, std::size_t chunk, std::vector<storage_sample> &samples) {
    if (options.workload == "all" || options.workload == "bstr") {
        const auto value = make_binary(budget);
        const auto wire  = encode_fixture(value, budget);
        run_storage_workload(options, "bstr", value, wire, chunk, samples);
    }
    if (options.workload == "all" || options.workload == "scalars") {
        const auto value = storage_scalars(budget);
        auto       size  = header_size(value.size());
        for (const auto scalar : value) {
            size += header_size(scalar);
        }
        const auto wire = encode_fixture(value, size);
        run_storage_workload(options, "scalars", value, wire, chunk, samples);
    }
}

storage_options parse_storage_options(int argc, char **argv) {
    storage_options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--self-test") {
            options.self_test = true;
            continue;
        }
        if (argument == "--help") {
            std::cout << "--bytes N --repetitions N --epochs N --workload all|bstr|scalars\n"
                         "--case all|vector|deque|span|vector-vector|deque-span|list-span|list-deque|vector-list|list-list|ring\n"
                         "--direction all|encode|decode --admission all|complete|1460 --self-test\n"
                         "--profile-iterations N (positive N; one epoch of N callbacks per row, no warmup)\n";
            std::exit(0);
        }
        require(index + 1 < argc, "missing option value");
        const std::string_view value{argv[++index]};
        if (argument == "--bytes") {
            options.bytes = parse_size(value);
        } else if (argument == "--repetitions") {
            options.repetitions = parse_size(value);
        } else if (argument == "--epochs") {
            options.epochs = parse_size(value);
        } else if (argument == "--profile-iterations") {
            options.profile_iterations = parse_size(value);
        } else if (argument == "--case") {
            options.selected_case = value;
        } else if (argument == "--workload") {
            options.workload = value;
        } else if (argument == "--direction") {
            options.direction = value;
        } else if (argument == "--admission") {
            options.admission = value;
        } else {
            throw std::runtime_error("unknown option: " + std::string{argument});
        }
    }
    require(options.selected_case == "all" || std::ranges::find(case_names, options.selected_case) != case_names.end(), "unknown case");
    require(options.workload == "all" || options.workload == "bstr" || options.workload == "scalars", "unknown workload");
    require(options.direction == "all" || options.direction == "encode" || options.direction == "decode", "unknown direction");
    require(options.admission == "all" || options.admission == "complete" || options.admission == "1460", "unknown admission mode");
    require(!(options.selected_case == "ring" && options.admission == "complete"), "ring supports bounded admission only");
    require(!(options.direction == "encode" && options.admission == "1460" &&
              (options.selected_case == "vector" || options.selected_case == "deque")),
            "flat growable output uses ordinary append");
    return options;
}

void print_storage_summaries(const std::vector<storage_sample> &samples) {
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto &key  = samples[index].key;
        const auto  same = [&](const storage_sample &sample) { return sample.key == key; };
        if (std::any_of(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(index), same)) {
            continue;
        }
        std::vector<double> values;
        for (const auto &sample : samples) {
            if (same(sample)) {
                values.push_back(sample.ns_per_byte);
            }
        }
        std::sort(values.begin(), values.end());
        const auto median = (values[(values.size() - 1) / 2] + values[values.size() / 2]) / 2;
        std::cout << "summary," << key << ",samples=" << values.size() << ",median_ns/B=" << median << ",min_ns/B=" << values.front()
                  << ",max_ns/B=" << values.back() << ",median_MB/s=" << 1000 / median << '\n';
    }
}

} // namespace resumable_bench

int main(int argc, char **argv) {
    using namespace resumable_bench;
    try {
        const auto                  options = parse_storage_options(argc, argv);
        std::vector<storage_sample> samples;
        std::cout << std::fixed << std::setprecision(9);
        if (options.self_test) {
            // Small and empty payloads, header cuts, empty descriptors, and ring wraparound.
            for (const auto budget : {1U, 17U, 1460U, 4096U}) {
                for (const auto chunk : {1U, 17U, 1460U}) {
                    run_fixtures(options, budget, chunk, samples);
                }
            }
            std::cout << "storage fixtures passed: values, wire bytes, completion, commitment, empty admission, ring reclamation\n";
        } else {
            std::cout << "workload,direction,case,admission,timed_storage_scope,repetition,encoded_bytes,MB/s,ns/B,epoch_min_ns/B,"
                         "epoch_max_ns/B,epochs,iterations\n";
            run_fixtures(options, options.bytes, packet_bytes, samples);
            require(!samples.empty(), "selected filters have no measurement rows");
            print_storage_summaries(samples);
        }
    } catch (const std::exception &error) {
        std::cerr << "storage benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
