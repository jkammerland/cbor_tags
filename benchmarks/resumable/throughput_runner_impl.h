#pragma once

#include "codec.h"
#include "throughput_runner.h"

#include <bit>
#include <chrono>
#include <iostream>
#include <nanobench.h>
#include <optional>
#include <utility>

namespace resumable_bench {
namespace {

template <wire_format Format>
inline constexpr bool typed_format = Format == wire_format::typed_native || Format == wire_format::typed_opposite;

template <typename T>
concept has_value_storage = requires(T &value) { value.values(); };

template <wire_format Format, typename T> decltype(auto) codec_value(T &value) {
    if constexpr (Format == wire_format::indefinite) {
        return ct::as_indefinite{value};
    } else if constexpr (typed_format<Format>) {
        constexpr bool native_little = std::endian::native == std::endian::little;
        constexpr bool little        = Format == wire_format::typed_native ? native_little : !native_little;
        if constexpr (has_value_storage<T>) {
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
    if constexpr (has_value_storage<T>) {
        return value.values();
    } else {
        return (value);
    }
}

template <typename T, typename Destination> bool decoded_equal(const T &expected, Destination &actual) {
    return expected == destination_storage(actual);
}

template <bool Resumable, wire_format Format = wire_format::ordinary, typename T>
bool decode_variant(const variant &mode, const bytes &encoded, T &destination) {
    auto &&wrapped = codec_value<Format>(destination);
#ifndef CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY
    if constexpr (Resumable) {
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

template <bool Resumable, wire_format Format = wire_format::ordinary, typename T>
bool encode_variant(const variant &mode, const T &value, bytes &encoded) {
    auto &&wrapped = codec_value<Format>(value);
#ifndef CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY
    if constexpr (Resumable) {
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

template <typename T> std::size_t scalar_wire_size(T value) {
    if constexpr (std::same_as<T, bool>)
        return 1;
    else if constexpr (std::same_as<T, float>)
        return 5;
    else if constexpr (std::same_as<T, double>)
        return 9;
    else
        return header_size(value);
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
                    expected += scalar_wire_size(key) + scalar_wire_size(mapped);
                }
                require(encoded.size() == expected, "map byte count mismatch");
            } else if constexpr (std::same_as<T, std::vector<std::uint32_t>> || std::same_as<T, std::vector<float>> ||
                                 std::same_as<T, std::vector<double>> || std::same_as<T, std::deque<bool>>) {
                std::size_t expected = Format == wire_format::indefinite ? 2 : header_size(value.size());
                for (const auto item : value) {
                    expected += scalar_wire_size(item);
                }
                require(encoded.size() == expected, "scalar array byte count mismatch");
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
template <typename T> auto counted_destination(const std::deque<T> &, allocation_counter &resource) {
    // An untyped resource pointer selects initializer_list<bool> for deque<bool>.
    return std::pmr::deque<T>{std::pmr::polymorphic_allocator<T>{&resource}};
}
inline auto counted_destination(const std::string &, allocation_counter &resource) { return std::pmr::string{&resource}; }

template <typename K, typename V> auto counted_destination(const std::map<K, V> &, allocation_counter &resource) {
    return std::pmr::map<K, V>{&resource};
}
template <typename K, typename V> auto counted_destination(const std::unordered_map<K, V> &, allocation_counter &resource) {
    return std::pmr::unordered_map<K, V>{&resource};
}

template <typename T>
concept has_counted_destination = requires(const T &value, allocation_counter &resource) { counted_destination(value, resource); };

template <typename T>
concept mapped_destination = requires { typename T::mapped_type; };

template <typename A, typename B> bool same_values(const A &expected, const B &actual) {
    if constexpr (mapped_destination<A>) {
        return expected.size() == actual.size() && std::ranges::all_of(expected, [&](const auto &entry) {
                   const auto found = actual.find(entry.first);
                   return found != actual.end() && found->second == entry.second;
               });
    } else {
        return std::ranges::equal(expected, actual);
    }
}

template <bool Resumable> bool validate_variant(const variant &mode, const bytes &encoded) {
#ifndef CBOR_TAGS_BENCHMARK_ONE_SHOT_ONLY
    if constexpr (Resumable) {
        return validate_resumable(encoded, mode.chunk_size);
    }
#else
    (void)mode;
#endif
    return validate_one_shot(encoded);
}

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

template <bool Resumable, wire_format Format, typename T>
bool run_mode(const throughput_options &options, const std::string &name, const T &value, const bytes &encoded, bytes &output,
              const variant &mode, runner_phase phase, std::size_t repetition, std::vector<sample> &samples) {
    using Destination = decltype(make_destination<Format, T>());

    // Preflight every mode before the driver starts any measurements.
    if (phase == runner_phase::preflight) {
        Destination decoded;
        require(decode_variant<Resumable, Format>(mode, encoded, decoded) && decoded_equal(value, decoded), "decode preflight mismatch");
        if constexpr (Format != wire_format::string_chunks) {
            require(encode_variant<Resumable, Format>(mode, value, output) && output == encoded, "encode preflight mismatch");
        }
        require(validate_variant<Resumable>(mode, encoded), "validation preflight failed");
        if constexpr (reservable_destination<T>) {
            destination_storage(decoded).clear();
            destination_storage(decoded).reserve(value.size());
            require(decode_variant<Resumable, Format>(mode, encoded, decoded) && decoded_equal(value, decoded),
                    "reserved decode preflight mismatch");
        }
        return true;
    }

    if (phase == runner_phase::allocations) {
        if constexpr (!typed_format<Format> && has_counted_destination<T>) {
            for (const bool reserve : {false, true}) {
                if (reserve && !reservable_destination<T>) {
                    continue;
                }
                allocation_counter resource;
                auto               decoded = counted_destination(value, resource);
                require(decoded.empty(), "allocation destination must start empty");
                require(decoded.get_allocator().resource() == &resource, "allocation destination must use its counter");
                if constexpr (reservable_destination<T>) {
                    if (reserve) {
                        decoded.reserve(value.size());
                    }
                }
                resource.allocations = resource.allocated_bytes = 0;
                require(decode_variant<Resumable, Format>(mode, encoded, decoded) && same_values(value, decoded),
                        "allocation trace mismatch");
                std::cout << "allocation," << name << ',' << mode.name << ',' << (reserve ? "reserved" : "cold") << ',' << encoded.size()
                          << ',' << resource.allocations << ',' << resource.allocated_bytes << '\n';
            }
            return true;
        } else {
            return false;
        }
    }

    ankerl::nanobench::Bench bench;
    bench.output(nullptr).unit("byte").batch(encoded.size()).performanceCounters(false);
    bench.epochs(options.epochs).minEpochTime(std::chrono::milliseconds{20}).minEpochIterations(1);

    // Fixed work bypasses nanobench's warmup and adaptive iteration selection for profiling.
    if (options.profile_iterations != 0) {
        bench.epochs(1).warmup(0).epochIterations(options.profile_iterations);
    }

    if (options.direction == "all" || options.direction == "decode") {
        if (options.storage != "reserved") {
            std::optional<Destination> decoded;
            bool                       succeeded = true;
            bench.run("decode", [&] {
                // Fresh storage includes destruction of the preceding result and new allocations.
                decoded.emplace();
                succeeded &= decode_variant<Resumable, Format>(mode, encoded, *decoded);
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
                    succeeded &= decode_variant<Resumable, Format>(mode, encoded, decoded);
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
                succeeded &= encode_variant<Resumable, Format>(mode, value, output);
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
            succeeded &= validate_variant<Resumable>(mode, encoded);
            ankerl::nanobench::doNotOptimizeAway(succeeded);
        });
        require(succeeded, "measured validation failed");
        print_measurement(name, "validate", mode, repetition + 1, encoded.size(), bench.results().back(), samples);
    }
    return true;
}

} // namespace
} // namespace resumable_bench
