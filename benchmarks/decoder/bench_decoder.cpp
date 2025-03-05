#include "cbor_tags/cbor_simple.h"
#include "cbor_tags/float16_ieee754.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <nameof.hpp>
#include <random>
#include <vector>
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <nanobench.h>
#include <small_generator.h>

using namespace std::string_view_literals;
using namespace cbor::tags;

struct benchmark_options {
    std::string_view unit{"Ops"};
    bool             relative{true};
};

// Function to run nanobench decoding benchmarks
template <typename Buffer> void run_decoding_benchmarks(ankerl::nanobench::Bench &bench) {
    auto seed = std::random_device{}();
    auto gen  = rng::small_generator{seed};

    bench.run("Decoding a uint", [&gen]() {
        // Prepare encoded data
        Buffer data;
        auto   enc      = make_encoder(data);
        auto   original = gen();
        enc(original);

        // Decode and benchmark
        auto     dec = make_decoder(data);
        uint64_t value;
        dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a uint with check", [&gen]() {
        Buffer data;
        auto   enc      = make_encoder(data);
        auto   original = gen();
        enc(original);

        auto     dec = make_decoder(data);
        uint64_t value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a int", [&gen]() {
        Buffer data;
        auto   enc      = make_encoder(data);
        auto   original = static_cast<int64_t>(gen());
        enc(original);

        auto    dec = make_decoder(data);
        int64_t value;
        dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a int with check", [&gen]() {
        Buffer data;
        auto   enc      = make_encoder(data);
        auto   original = static_cast<int64_t>(gen());
        enc(original);

        auto    dec = make_decoder(data);
        int64_t value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a bstr", [&gen]() {
        Buffer                   data;
        auto                     enc = make_encoder(data);
        std::array<std::byte, 4> original;
        for (auto &b : original) {
            b = std::byte(gen());
        }
        enc(original);

        auto                     dec = make_decoder(data);
        std::array<std::byte, 4> value;
        dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a bstr with check", [&gen]() {
        Buffer                   data;
        auto                     enc = make_encoder(data);
        std::array<std::byte, 4> original;
        for (auto &b : original) {
            b = std::byte(gen());
        }
        enc(original);

        auto                     dec = make_decoder(data);
        std::array<std::byte, 4> value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a tstr", [&gen]() {
        Buffer      data;
        auto        enc = make_encoder(data);
        std::string original;
        original.resize(4);
        std::ranges::generate(original, gen);
        enc(original);

        auto        dec = make_decoder(data);
        std::string value;
        dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a tstr with check", [&gen]() {
        Buffer      data;
        auto        enc = make_encoder(data);
        std::string original;
        original.resize(4);
        std::ranges::generate(original, gen);
        enc(original);

        auto        dec = make_decoder(data);
        std::string value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding an array", [&gen]() {
        Buffer                 data;
        auto                   enc = make_encoder(data);
        std::array<int16_t, 4> original{static_cast<short>(gen()), static_cast<short>(gen()), static_cast<short>(gen()),
                                        static_cast<short>(gen())};
        enc(original);

        auto                   dec = make_decoder(data);
        std::array<int16_t, 4> value;
        dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding an array with check", [&gen]() {
        Buffer                 data;
        auto                   enc = make_encoder(data);
        std::array<int16_t, 4> original{static_cast<short>(gen()), static_cast<short>(gen()), static_cast<short>(gen()),
                                        static_cast<short>(gen())};
        enc(original);

        auto                   dec = make_decoder(data);
        std::array<int16_t, 4> value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a map", [&gen]() {
        Buffer                     data;
        auto                       enc = make_encoder(data);
        std::map<int16_t, int32_t> original{{static_cast<short>(gen()), static_cast<int>(gen())},
                                            {static_cast<short>(gen()), static_cast<int>(gen())}};
        enc(original);

        auto                       dec = make_decoder(data);
        std::map<int16_t, int32_t> value;
        dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a map with check", [&gen]() {
        Buffer                     data;
        auto                       enc = make_encoder(data);
        std::map<int16_t, int32_t> original{{static_cast<short>(gen()), static_cast<int>(gen())},
                                            {static_cast<short>(gen()), static_cast<int>(gen())}};
        enc(original);

        auto                       dec = make_decoder(data);
        std::map<int16_t, int32_t> value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    struct A {
        static_tag<333> cbor_tag;
        int64_t         value;
    };

    bench.run("Decoding a tag", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        A      original{.cbor_tag = {}, .value = static_cast<int64_t>(gen())};
        enc(original);

        auto dec = make_decoder(data);
        A    value;
        dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a tag with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        A      original{.cbor_tag = {}, .value = static_cast<int64_t>(gen())};
        enc(original);

        auto dec = make_decoder(data);
        A    value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a nullptr", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        enc(std::nullptr_t{});

        auto           dec = make_decoder(data);
        std::nullptr_t value;
        dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a bool with check", [&gen]() {
        Buffer data;
        auto   enc      = make_encoder(data);
        bool   original = static_cast<bool>(gen() % 2);
        enc(original);

        auto dec = make_decoder(data);
        bool value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a float16", [&gen]() {
        Buffer data;
        auto   enc      = make_encoder(data);
        auto   original = static_cast<float16_t>(static_cast<float>(gen()));
        enc(original);

        auto      dec = make_decoder(data);
        float16_t value;
        dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a float32", [&gen]() {
        Buffer data;
        auto   enc      = make_encoder(data);
        auto   original = static_cast<float>(gen());
        enc(original);

        auto  dec = make_decoder(data);
        float value;
        dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a float64", [&gen]() {
        Buffer data;
        auto   enc      = make_encoder(data);
        auto   original = static_cast<double>(gen());
        enc(original);

        auto   dec = make_decoder(data);
        double value;
        dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a float16_t with check", [&gen]() {
        Buffer data;
        auto   enc      = make_encoder(data);
        auto   original = static_cast<float16_t>(static_cast<float>(gen()));
        enc(original);

        auto      dec = make_decoder(data);
        float16_t value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a float32 with check", [&gen]() {
        Buffer data;
        auto   enc      = make_encoder(data);
        auto   original = static_cast<float>(gen());
        enc(original);

        auto  dec = make_decoder(data);
        float value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a double with check", [&gen]() {
        Buffer data;
        auto   enc      = make_encoder(data);
        auto   original = static_cast<double>(gen());
        enc(original);

        auto   dec = make_decoder(data);
        double value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a simple with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        simple original{static_cast<simple::value_type>(gen())};
        enc(original);

        auto   dec = make_decoder(data);
        simple value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });
}

template <typename ContainerType> void run_decoding_benchmark() {
    ankerl::nanobench::Bench bench;
    benchmark_options        options;

    bench.title(std::string(nameof::nameof_type<ContainerType>()) + " buffer (decoding)");
    bench.minEpochIterations(100);
    bench.unit(options.unit.data());
    bench.performanceCounters(true);
    bench.relative(options.relative);

    run_decoding_benchmarks<ContainerType>(bench);
}

TEST_CASE("Decoding benchmarks") {
    run_decoding_benchmark<std::vector<uint8_t>>();
    run_decoding_benchmark<std::deque<uint8_t>>();
    run_decoding_benchmark<std::array<uint8_t, 20>>();
}

// Main function to run tests and benchmarks
int main(int argc, char **argv) {
    // Run Doctest tests
    int result = doctest::Context(argc, argv).run();
    return result;
}