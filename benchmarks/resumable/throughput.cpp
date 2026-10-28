#include "codec.h"

#include <bit>
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
    std::string storage     = "cold";
    std::string direction   = "all";
    std::string transport   = "all";
    std::size_t cbor_chunk  = 256;
    bool        allocations = false;
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
        if (options.transport == "all" || options.transport == "full") {
            result.push_back({"resumable-full", wire_bytes});
        }
        for (const auto chunk : {1460U, 4096U, 65536U}) {
            if (options.transport == "all" || options.transport == std::to_string(chunk)) {
                result.push_back({"resumable-" + std::to_string(chunk), chunk});
            }
        }
    }
#endif
    require(!result.empty(), "resumable mode is unavailable in a one-shot-only build");
    return result;
}

enum class wire_format { ordinary, indefinite, typed_native, typed_opposite, string_chunks };

template <wire_format Format>
inline constexpr bool typed_format = Format == wire_format::typed_native || Format == wire_format::typed_opposite;

template <wire_format Format, typename T> decltype(auto) codec_value(T &value) {
    if constexpr (Format == wire_format::indefinite) {
        return ct::as_indefinite{value};
    } else if constexpr (typed_format<Format>) {
        constexpr bool native_little = std::endian::native == std::endian::little;
        constexpr bool little        = Format == wire_format::typed_native ? native_little : !native_little;
        if constexpr (requires { value.values(); }) {
            return (value);
        } else if constexpr (little) {
            return ct::rfc8746::as_typed_array(value);
        } else {
            return ct::rfc8746::as_typed_array_be(value);
        }
    } else {
        return (value);
    }
}

template <wire_format Format, typename T> auto make_destination() {
    if constexpr (typed_format<Format>) {
        constexpr bool native_little = std::endian::native == std::endian::little;
        constexpr bool little        = Format == wire_format::typed_native ? native_little : !native_little;
        constexpr auto order         = little ? ct::rfc8746::typed_array_byte_order::little : ct::rfc8746::typed_array_byte_order::big;
        return ct::rfc8746::typed_array<std::uint32_t, order>{};
    } else {
        return T{};
    }
}

template <typename T> decltype(auto) destination_storage(T &value) {
    if constexpr (requires { value.values(); }) {
        return value.values();
    } else {
        return (value);
    }
}

template <typename T, typename Destination> bool decoded_equal(const T &expected, Destination &actual) {
    return expected == destination_storage(actual);
}

template <wire_format Format = wire_format::ordinary, typename T>
bool decode_variant(const variant &mode, const bytes &encoded, T &destination) {
    auto &&wrapped = codec_value<Format>(destination);
#ifndef CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY
    if (mode.chunk_size != 0) {
        if constexpr (typed_format<Format>) {
            return decode_resumable<ct::codec::typed_array>(encoded, wrapped, mode.chunk_size);
        } else {
            return decode_resumable(encoded, wrapped, mode.chunk_size);
        }
    }
#else
    (void)mode;
#endif
    if constexpr (typed_format<Format>) {
        return decode_one_shot<ct::codec::typed_array>(encoded, wrapped);
    } else {
        return decode_one_shot(encoded, wrapped);
    }
}

template <wire_format Format = wire_format::ordinary, typename T> bool encode_variant(const variant &mode, const T &value, bytes &encoded) {
    auto &&wrapped = codec_value<Format>(value);
#ifndef CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY
    if (mode.chunk_size != 0) {
        if constexpr (typed_format<Format>) {
            return encode_resumable<ct::codec::typed_array>(wrapped, encoded, mode.chunk_size);
        } else {
            return encode_resumable(wrapped, encoded, mode.chunk_size);
        }
    }
#else
    (void)mode;
#endif
    if constexpr (typed_format<Format>) {
        return encode_one_shot<ct::codec::typed_array>(wrapped, encoded);
    } else {
        return encode_one_shot(wrapped, encoded);
    }
}

template <wire_format Format, typename T>
bytes fixture_bytes(const T &value, const throughput_options &options, std::optional<std::size_t> exact_size) {
    if constexpr (Format == wire_format::string_chunks) {
        return encode_string_chunks(value, options.cbor_chunk);
    } else {
        auto &&wrapped = codec_value<Format>(value);
        if constexpr (typed_format<Format>) {
            auto encoded = encode_fixture<ct::codec::typed_array>(wrapped);
            require(encoded.size() ==
                        2 + header_size(value.size() * sizeof(typename T::value_type)) + value.size() * sizeof(typename T::value_type),
                    "typed array byte count mismatch");
            return encoded;
        } else {
            auto encoded = encode_fixture(wrapped, exact_size);
            if constexpr (ct::IsMap<T>) {
                std::size_t expected = Format == wire_format::indefinite ? 2 : header_size(value.size());
                for (const auto &[key, mapped] : value) {
                    expected += header_size(key) + header_size(mapped);
                }
                require(encoded.size() == expected, "map byte count mismatch");
            } else if constexpr (std::same_as<T, std::vector<std::uint32_t>>) {
                std::size_t expected = Format == wire_format::indefinite ? 2 : header_size(value.size());
                for (const auto item : value) {
                    expected += header_size(item);
                }
                require(encoded.size() == expected, "integer array byte count mismatch");
            }
            return encoded;
        }
    }
}

template <typename T>
concept reservable_destination = requires(T &value) {
    value.reserve(std::size_t{});
    value.clear();
};

template <typename T> auto counted_destination(const std::vector<T> &, allocation_counter &resource) {
    return std::pmr::vector<T>{&resource};
}
inline auto counted_destination(const std::string &, allocation_counter &resource) { return std::pmr::string{&resource}; }

template <typename K, typename V> auto counted_destination(const std::map<K, V> &, allocation_counter &resource) {
    return std::pmr::map<K, V>{&resource};
}
template <typename K, typename V> auto counted_destination(const std::unordered_map<K, V> &, allocation_counter &resource) {
    return std::pmr::unordered_map<K, V>{&resource};
}

template <typename A, typename B> bool same_values(const A &expected, const B &actual) {
    if constexpr (requires { typename A::mapped_type; }) {
        return expected.size() == actual.size() && std::ranges::all_of(expected, [&](const auto &entry) {
                   const auto found = actual.find(entry.first);
                   return found != actual.end() && found->second == entry.second;
               });
    } else {
        return std::ranges::equal(expected, actual);
    }
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

template <wire_format Format = wire_format::ordinary, typename T>
void run_workload(const throughput_options &options, const std::string &name, const T &value, std::optional<std::size_t> exact_size,
                  std::vector<sample> &samples) {
    using Destination  = decltype(make_destination<Format, T>());
    const auto encoded = fixture_bytes<Format>(value, options, exact_size);
    auto       modes   = variants(options, encoded.size());
    bytes      output(encoded.size());

    // Check typed values independently of re-encoding, then verify exact wire output.
    for (const auto &mode : modes) {
        Destination decoded;
        require(decode_variant<Format>(mode, encoded, decoded) && decoded_equal(value, decoded), "decode preflight mismatch");
        if constexpr (Format != wire_format::string_chunks) {
            require(encode_variant<Format>(mode, value, output) && output == encoded, "encode preflight mismatch");
        }
        require(validate_variant(mode, encoded), "validation preflight failed");
        if constexpr (reservable_destination<T>) {
            destination_storage(decoded).clear();
            destination_storage(decoded).reserve(value.size());
            require(decode_variant<Format>(mode, encoded, decoded) && decoded_equal(value, decoded), "reserved decode preflight mismatch");
        }
    }
    if (options.self_test) {
        std::cout << name << ": fixture and codec paths passed (" << encoded.size() << " bytes)\n";
        return;
    }
    if (options.allocations) {
        if constexpr (!typed_format<Format> && requires(allocation_counter &resource) { counted_destination(value, resource); }) {
            for (const auto &mode : modes) {
                for (const bool reserve : {false, true}) {
                    if (reserve && !reservable_destination<T>) {
                        continue;
                    }
                    allocation_counter resource;
                    auto               decoded = counted_destination(value, resource);
                    if constexpr (reservable_destination<T>) {
                        if (reserve) {
                            decoded.reserve(value.size());
                        }
                    }
                    resource.allocations = resource.allocated_bytes = 0;
                    require(decode_variant<Format>(mode, encoded, decoded) && same_values(value, decoded), "allocation trace mismatch");
                    std::cout << "allocation," << name << ',' << mode.name << ',' << (reserve ? "reserved" : "cold") << ','
                              << encoded.size() << ',' << resource.allocations << ',' << resource.allocated_bytes << '\n';
                }
            }
        } else {
            std::cout << "allocation-unavailable," << name << '\n';
        }
        return;
    }

    for (std::size_t repetition = 0; repetition < options.repetitions; ++repetition) {
        if (repetition != 0) {
            std::reverse(modes.begin(), modes.end());
        }
        for (const auto &mode : modes) {
            ankerl::nanobench::Bench bench;
            bench.output(nullptr).unit("byte").batch(encoded.size()).performanceCounters(false);
            bench.epochs(options.epochs).minEpochTime(std::chrono::milliseconds{20}).minEpochIterations(1);

            if (options.direction == "all" || options.direction == "decode") {
                if (options.storage != "reserved") {
                    std::optional<Destination> decoded;
                    bool                       succeeded = true;
                    bench.run("decode", [&] {
                        // Fresh storage includes destruction of the preceding result and new allocations.
                        decoded.emplace();
                        succeeded &= decode_variant<Format>(mode, encoded, *decoded);
                        ankerl::nanobench::doNotOptimizeAway(*decoded);
                        ankerl::nanobench::doNotOptimizeAway(succeeded);
                    });
                    require(succeeded && decoded && decoded_equal(value, *decoded), "measured decode mismatch");
                    print_measurement(name, "decode", mode, repetition + 1, encoded.size(), bench.results().back(), samples);
                }
                if constexpr (reservable_destination<T>) {
                    if (options.storage != "cold") {
                        Destination decoded;
                        destination_storage(decoded).reserve(value.size());
                        bool succeeded = true;
                        bench.run("decode-reserved", [&] {
                            // Capacity is supplied outside timing; clearing prior elements remains timed.
                            destination_storage(decoded).clear();
                            succeeded &= decode_variant<Format>(mode, encoded, decoded);
                            ankerl::nanobench::doNotOptimizeAway(decoded);
                            ankerl::nanobench::doNotOptimizeAway(succeeded);
                        });
                        require(succeeded && decoded_equal(value, decoded), "measured reserved decode mismatch");
                        print_measurement(name, "decode-reserved", mode, repetition + 1, encoded.size(), bench.results().back(), samples);
                    }
                }
            }
            if constexpr (Format != wire_format::string_chunks) {
                if (options.direction == "all" || options.direction == "encode") {
                    bool succeeded = true;
                    bench.run("encode", [&] {
                        succeeded &= encode_variant<Format>(mode, value, output);
                        ankerl::nanobench::doNotOptimizeAway(output.data());
                        ankerl::nanobench::doNotOptimizeAway(succeeded);
                    });
                    require(succeeded && output == encoded, "measured encode mismatch");
                    print_measurement(name, "encode", mode, repetition + 1, encoded.size(), bench.results().back(), samples);
                }
            }
            if (options.direction == "all" || options.direction == "validate") {
                bool succeeded = true;
                bench.run("validate", [&] {
                    succeeded &= validate_variant(mode, encoded);
                    ankerl::nanobench::doNotOptimizeAway(succeeded);
                });
                require(succeeded, "measured validation failed");
                print_measurement(name, "validate", mode, repetition + 1, encoded.size(), bench.results().back(), samples);
            }
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
        if (argument == "--allocations") {
            options.allocations = true;
            continue;
        }
        if (argument == "--self-test") {
            options.self_test = true;
            continue;
        }
        if (argument == "--help") {
            std::cout << "--bytes N --repetitions N --epochs N --workload "
                         "all|structured|bstr|tstr|scalars|scalars-indef|map|map-indef|unordered-map|unordered-map-indef|typed-native|"
                         "typed-opposite|bstr-indef|tstr-indef\n"
                         "--mode all|one-shot|resumable --storage cold|reserved|both --direction all|encode|decode|validate\n"
                         "--transport all|full|1460|4096|65536 --cbor-chunk N --allocations --self-test\n";
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
        } else if (argument == "--storage") {
            options.storage = value;
        } else if (argument == "--direction") {
            options.direction = value;
        } else if (argument == "--transport") {
            options.transport = value;
        } else if (argument == "--cbor-chunk") {
            options.cbor_chunk = parse_size(value);
        } else if (argument == "--mode") {
            options.mode = value;
        } else {
            throw std::runtime_error("unknown option: " + std::string{argument});
        }
    }
    require(options.workload == "all" || options.workload == "structured" || options.workload == "bstr" || options.workload == "tstr" ||
                options.workload == "scalars" || options.workload == "scalars-indef" || options.workload == "map" ||
                options.workload == "map-indef" || options.workload == "unordered-map" || options.workload == "unordered-map-indef" ||
                options.workload == "typed-native" || options.workload == "typed-opposite" || options.workload == "bstr-indef" ||
                options.workload == "tstr-indef",
            "unknown workload");
    require(options.mode == "all" || options.mode == "one-shot" || options.mode == "resumable", "unknown mode");
    require(options.storage == "cold" || options.storage == "reserved" || options.storage == "both", "unknown storage mode");
    require(options.direction == "all" || options.direction == "encode" || options.direction == "decode" || options.direction == "validate",
            "unknown direction");
    require(options.transport == "all" || options.transport == "full" || options.transport == "1460" || options.transport == "4096" ||
                options.transport == "65536",
            "unknown transport chunk");
    require(!(options.direction == "encode" && (options.workload == "bstr-indef" || options.workload == "tstr-indef")),
            "chunked string fixtures are decode/validate workloads");
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
        std::cout << std::fixed << std::setprecision(9);
        if (options.allocations) {
            std::cout << "kind,workload,mode,storage,encoded_bytes,allocations,allocated_bytes\n";
        } else if (!options.self_test) {
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
        if (options.workload == "all" || options.workload == "scalars-indef") {
            run_workload<wire_format::indefinite>(options, "scalars-indef", make_scalars(options.bytes), {}, samples);
        }
        if (options.workload == "all" || options.workload == "map") {
            run_workload(options, "map", make_integer_map<std::map<std::uint32_t, std::uint32_t>>(options.bytes), {}, samples);
        }
        if (options.workload == "all" || options.workload == "unordered-map") {
            run_workload(options, "unordered-map", make_integer_map<std::unordered_map<std::uint32_t, std::uint32_t>>(options.bytes), {},
                         samples);
        }
        if (options.workload == "all" || options.workload == "map-indef") {
            run_workload<wire_format::indefinite>(options, "map-indef",
                                                  make_integer_map<std::map<std::uint32_t, std::uint32_t>>(options.bytes), {}, samples);
        }
        if (options.workload == "all" || options.workload == "unordered-map-indef") {
            run_workload<wire_format::indefinite>(options, "unordered-map-indef",
                                                  make_integer_map<std::unordered_map<std::uint32_t, std::uint32_t>>(options.bytes), {},
                                                  samples);
        }
        if (options.workload == "all" || options.workload == "typed-native") {
            run_workload<wire_format::typed_native>(options, "typed-native", make_scalars(options.bytes), {}, samples);
        }
        if (options.workload == "all" || options.workload == "typed-opposite") {
            run_workload<wire_format::typed_opposite>(options, "typed-opposite", make_scalars(options.bytes), {}, samples);
        }
        for (const auto chunk : options.self_test ? std::vector<std::size_t>{1, 17, 4096} : std::vector<std::size_t>{options.cbor_chunk}) {
            auto chunk_options       = options;
            chunk_options.cbor_chunk = chunk;
            if (options.workload == "all" || options.workload == "bstr-indef") {
                run_workload<wire_format::string_chunks>(chunk_options, "bstr-indef-" + std::to_string(chunk), make_binary(options.bytes),
                                                         {}, samples);
            }
            if (options.workload == "all" || options.workload == "tstr-indef") {
                run_workload<wire_format::string_chunks>(chunk_options, "tstr-indef-" + std::to_string(chunk), make_text(options.bytes), {},
                                                         samples);
            }
        }
        if (!options.self_test && !options.allocations) {
            require(!samples.empty(), "selected workload/storage/direction combination has no measurement rows");
        }
        print_summaries(samples);
    } catch (const std::exception &error) {
        std::cerr << "benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
