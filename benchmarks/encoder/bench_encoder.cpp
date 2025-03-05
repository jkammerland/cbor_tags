#include "cbor_tags/cbor_simple.h"
#include "cbor_tags/float16_ieee754.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <nameof.hpp>
#include <random>
#include <vector>
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

#include <cbor_tags/cbor_encoder.h>
#include <nanobench.h>
#include <small_generator.h>

using namespace std::string_view_literals;
using namespace cbor::tags;

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
        auto   enc = make_encoder(data);
        enc(gen());
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
        auto   enc = make_encoder(data);
        enc(static_cast<int64_t>(gen()));
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
        enc(bstr);
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
        std::ranges::generate(s, gen);
        enc(s);
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encoding a tstr with check", [&gen]() {
        Buffer      data;
        auto        enc = make_encoder(data);
        std::string s;
        s.resize(4);
        std::ranges::generate(s, gen);
        CHECK(enc(s));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encoding a array", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        enc(std::array<int16_t, 4>{static_cast<short>(gen()), static_cast<short>(gen()), static_cast<short>(gen()),
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
        auto   enc = make_encoder(data);
        enc(std::map<int16_t, int32_t>{{static_cast<short>(gen()), static_cast<int>(gen())},
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
        auto   enc = make_encoder(data);
        enc(A{.cbor_tag = {}, .value = static_cast<int64_t>(gen())});
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a tag with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        CHECK(enc(A{.cbor_tag = {}, .value = static_cast<int64_t>(gen())}));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a nullptr", [&gen]() {
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
        auto   enc = make_encoder(data);
        enc(static_cast<float16_t>(static_cast<float>(gen())));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a float32", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        enc(static_cast<float>(gen()));
        ankerl::nanobench::doNotOptimizeAway(data);
    });

    bench.run("Encode a float64", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        enc(static_cast<double>(gen()));
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

// Main function to run tests and benchmarks
int main(int argc, char **argv) {
    // Run Doctest tests
    int result = doctest::Context(argc, argv).run();
    return result;
}