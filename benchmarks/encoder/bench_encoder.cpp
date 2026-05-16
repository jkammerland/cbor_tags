#include "cbor_tags/cbor_simple.h"
#include "cbor_tags/float16_ieee754.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <nameof.hpp>
#include <random>
#include <string>
#include <vector>
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/smart_ptr.h>
#include <fmt/format.h>
#include <nanobench.h>
#include <small_generator.h>

using namespace std::string_view_literals;
using namespace cbor::tags;
using namespace cbor::tags::ext::smart_ptr;

struct benchmark_options {
    std::string_view unit{"Ops"};
    bool             relative{true};
};

// Function to run nanobench benchmark
template <typename Buffer> void run_encoding_benchmarks(ankerl::nanobench::Bench &bench) {
    auto seed = std::random_device{}();
    auto gen  = rng::small_generator{seed};

    bench.run("Encoding a uint", [&gen]() {
        Buffer data;
        auto   enc  = make_encoder(data);
        std::ignore = enc(gen());
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encoding a uint with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        CHECK(enc(gen()));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encoding a int", [&gen]() {
        Buffer data;
        auto   enc  = make_encoder(data);
        std::ignore = enc(static_cast<int64_t>(gen()));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encoding a int with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        CHECK(enc(static_cast<int64_t>(gen())));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encoding a bstr", [&gen]() {
        std::vector<uint8_t>     data;
        auto                     enc = make_encoder(data);
        std::array<std::byte, 4> bstr;
        for (auto &b : bstr) {
            b = std::byte(gen());
        }
        std::ignore = enc(bstr);
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encoding a bstr with check", [&gen]() {
        std::vector<uint8_t>     data;
        auto                     enc = make_encoder(data);
        std::array<std::byte, 4> bstr;
        for (auto &b : bstr) {
            b = std::byte(gen());
        }
        CHECK(enc(bstr));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encoding a tstr", [&gen]() {
        Buffer      data;
        auto        enc = make_encoder(data);
        std::string s;
        s.resize(4);
        std::transform(s.begin(), s.end(), s.begin(), [&gen](auto) { return static_cast<char>(gen.generate()); });
        std::ignore = enc(s);
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encoding a tstr with check", [&gen]() {
        Buffer      data;
        auto        enc = make_encoder(data);
        std::string s;
        s.resize(4);
        std::transform(s.begin(), s.end(), s.begin(), [&gen](auto) { return static_cast<char>(gen.generate()); });
        CHECK(enc(s));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encoding a array", [&gen]() {
        Buffer data;
        auto   enc  = make_encoder(data);
        std::ignore = enc(std::array<int16_t, 4>{static_cast<short>(gen()), static_cast<short>(gen()), static_cast<short>(gen()),
                                                 static_cast<short>(gen())});
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encoding an array with check", [&gen]() {
        std::vector<uint8_t>   data;
        auto                   enc = make_encoder(data);
        std::array<int16_t, 4> a{static_cast<short>(gen()), static_cast<short>(gen()), static_cast<short>(gen()),
                                 static_cast<short>(gen())};
        CHECK(enc(a));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a map", [&gen]() {
        Buffer data;
        auto   enc  = make_encoder(data);
        std::ignore = enc(std::map<int16_t, int32_t>{{static_cast<short>(gen()), static_cast<int>(gen())},
                                                     {static_cast<short>(gen()), static_cast<int>(gen())}});
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a map with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        CHECK(enc(std::map<int16_t, int32_t>{{static_cast<short>(gen()), static_cast<int>(gen())},
                                             {static_cast<short>(gen()), static_cast<int>(gen())}}));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    struct A {
        static_tag<333> cbor_tag;
        int64_t         value;
    };

    bench.run("Encode a tag", [&gen]() {
        Buffer data;
        auto   enc  = make_encoder(data);
        std::ignore = enc(A{.cbor_tag = {}, .value = static_cast<int64_t>(gen())});
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a tag with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        CHECK(enc(A{.cbor_tag = {}, .value = static_cast<int64_t>(gen())}));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a nullptr", []() {
        Buffer data;
        auto   enc = make_encoder(data);
        CHECK(enc(std::nullptr_t{}));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a bool with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        CHECK(enc(static_cast<bool>(gen() % 2)));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a float16", [&gen]() {
        Buffer data;
        auto   enc  = make_encoder(data);
        std::ignore = enc(static_cast<float16_t>(static_cast<float>(gen())));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a float32", [&gen]() {
        Buffer data;
        auto   enc  = make_encoder(data);
        std::ignore = enc(static_cast<float>(gen()));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a float64", [&gen]() {
        Buffer data;
        auto   enc  = make_encoder(data);
        std::ignore = enc(static_cast<double>(gen()));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a float16_t with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        CHECK(enc(static_cast<float16_t>(static_cast<float>(gen()))));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a float32 with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        CHECK(enc(static_cast<float>(gen())));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a double with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        CHECK(enc(static_cast<double>(gen())));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a simple with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        simple s{static_cast<simple::value_type>(gen())};
        CHECK(enc(s));
        ankerl::nanobench::doNotOptimizeAway(data);
    });
}

std::vector<std::shared_ptr<std::uint64_t>> make_shared_graph_values(std::size_t unique_count, std::size_t repeat_count) {
    std::vector<std::shared_ptr<std::uint64_t>> unique_values;
    unique_values.reserve(unique_count);
    for (std::size_t index = 0; index < unique_count; ++index) {
        unique_values.push_back(std::make_shared<std::uint64_t>(index));
    }

    std::vector<std::shared_ptr<std::uint64_t>> values;
    values.reserve(unique_count * repeat_count);
    for (std::size_t repeat = 0; repeat < repeat_count; ++repeat) {
        values.insert(values.end(), unique_values.begin(), unique_values.end());
    }
    return values;
}

void run_shared_graph_encode_lookup_benchmarks(ankerl::nanobench::Bench &bench) {
    const auto sizes = std::array<std::size_t, 4>{4U, 16U, 64U, 256U};

    for (const auto unique_count : sizes) {
        const auto values = make_shared_graph_values(unique_count, 2U);

        bench.run(fmt::format("shared_graph encode {} unique x2 unordered_map", unique_count), [&values]() {
            std::vector<std::uint8_t> data;
            data.reserve(values.size() * 8U);

            auto                        enc = make_encoder<shared_graph_codec>(data);
            shared_graph_encode_session graph{shared_graph_encode_lookup::unordered_map};
            auto                        result = enc(as_shared_graph(graph, values));

            ankerl::nanobench::doNotOptimizeAway(result);
            ankerl::nanobench::doNotOptimizeAway(data);
        });

        bench.run(fmt::format("shared_graph encode {} unique x2 vector_scan_o_n", unique_count), [&values]() {
            std::vector<std::uint8_t> data;
            data.reserve(values.size() * 8U);

            auto                        enc = make_encoder<shared_graph_codec>(data);
            shared_graph_encode_session graph{shared_graph_encode_lookup::linear_scan};
            auto                        result = enc(as_shared_graph(graph, values));

            ankerl::nanobench::doNotOptimizeAway(result);
            ankerl::nanobench::doNotOptimizeAway(data);
        });
    }
}

template <typename ContainerType> void run_benchmark() {
    ankerl::nanobench::Bench bench;
    benchmark_options        options;

    bench.title(std::string(nameof::nameof_type<ContainerType>()) + " buffer");
    bench.minEpochIterations(100);
    bench.unit(options.unit.data());
    bench.performanceCounters(true);
    bench.relative(options.relative);

    run_encoding_benchmarks<ContainerType>(bench);
}

TEST_CASE("Encoding benchmarks") {
    run_benchmark<std::vector<uint8_t>>();
    run_benchmark<std::deque<uint8_t>>();
    run_benchmark<std::array<uint8_t, 20>>();
}

TEST_CASE("Shared graph encode lookup benchmarks") {
    ankerl::nanobench::Bench bench;
    benchmark_options        options;

    bench.title("shared_graph encode lookup");
    bench.minEpochIterations(20);
    bench.unit(options.unit.data());
    bench.performanceCounters(true);
    bench.relative(options.relative);

    run_shared_graph_encode_lookup_benchmarks(bench);
}

// Main function to run tests and benchmarks
int main(int argc, char **argv) {
    // Run Doctest tests
    int result = doctest::Context(argc, argv).run();
    return result;
}
