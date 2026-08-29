#include "cbor_tags/cbor.h"
#include "cbor_tags/cbor_simple.h"
#include "cbor_tags/float16_ieee754.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fmt/base.h>
#include <map>
#include <nameof.hpp>
#include <random>
#include <vector>
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"
#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <nanobench.h>
#include <small_generator.h>
#include <variant>

using std::nullptr_t;

using namespace std::string_view_literals;
using namespace cbor::tags;

struct benchmark_options {
    std::string_view unit{"Ops"};
    bool             relative{true};
};

// Define a struct for the tagged type
struct A {
    static_tag<333> cbor_tag;
    int64_t         value;
};

template <size_t N> struct B {
    static_tag<N> cbor_tag;
    int64_t       value;
};

// Structure to hold pre-encoded test data
template <typename Buffer> struct PreEncodedData {
    Buffer uint_data;
    Buffer int_data;
    Buffer bstr_data;
    Buffer tstr_data;
    Buffer array_data;
    Buffer map_data;
    Buffer tag_data;
    Buffer nullptr_data;
    Buffer bool_data;
    Buffer float16_data;
    Buffer float32_data;
    Buffer float64_data;
    Buffer simple_data;
};

// Function to prepare all test data before benchmarking
template <typename Buffer> PreEncodedData<Buffer> prepare_test_data(rng::small_generator &gen) {
    PreEncodedData<Buffer> data;

    // Prepare uint data
    {
        auto original = gen();
        auto enc      = make_encoder(data.uint_data);
        std::ignore   = enc(original);
    }

    // Prepare int data
    {
        auto original = static_cast<int64_t>(gen());
        auto enc      = make_encoder(data.int_data);
        std::ignore   = enc(original);
    }

    // Prepare bstr data
    {
        std::array<std::byte, 4> original;
        for (auto &b : original) {
            b = std::byte(gen());
        }
        auto enc    = make_encoder(data.bstr_data);
        std::ignore = enc(original);
    }

    // Prepare tstr data
    {
        std::string s;
        s.resize(4);
        std::transform(s.begin(), s.end(), s.begin(), [&gen](auto) { return static_cast<char>(gen.generate()); });
        auto enc = make_encoder(data.tstr_data);
        CHECK(enc(s));
    }

    // Prepare array data
    {
        std::array<int16_t, 4> original{static_cast<short>(gen()), static_cast<short>(gen()), static_cast<short>(gen()),
                                        static_cast<short>(gen())};
        auto                   enc = make_encoder(data.array_data);
        std::ignore                = enc(original);
    }

    // Prepare map data
    {
        std::map<int16_t, int32_t> original{{static_cast<short>(gen()), static_cast<int>(gen())},
                                            {static_cast<short>(gen()), static_cast<int>(gen())}};
        auto                       enc = make_encoder(data.map_data);
        std::ignore                    = enc(original);
    }

    // Prepare tag data
    {
        A    original{.cbor_tag = {}, .value = static_cast<int64_t>(gen())};
        auto enc = make_encoder(data.tag_data);
        CHECK(enc(original));
    }

    // Prepare nullptr data
    {
        auto enc = make_encoder(data.nullptr_data);
        CHECK(enc(nullptr_t{}));
    }

    // Prepare bool data
    {
        bool original = static_cast<bool>(gen() % 2);
        auto enc      = make_encoder(data.bool_data);
        std::ignore   = enc(original);
    }

    // Prepare float16 data
    {
        auto original = static_cast<float16_t>(static_cast<float>(gen()));
        auto enc      = make_encoder(data.float16_data);
        std::ignore   = enc(original);
    }

    // Prepare float32 data
    {
        auto original = static_cast<float>(gen());
        auto enc      = make_encoder(data.float32_data);
        std::ignore   = enc(original);
    }

    // Prepare float64 data
    {
        auto original = static_cast<double>(gen());
        auto enc      = make_encoder(data.float64_data);
        std::ignore   = enc(original);
    }

    // Prepare simple data
    {
        simple original{static_cast<simple::value_type>(gen())};
        auto   enc  = make_encoder(data.simple_data);
        std::ignore = enc(original);
    }

    return data;
}

// Function to run nanobench decoding benchmarks
template <typename Buffer> void run_decoding_benchmarks(ankerl::nanobench::Bench &bench) {
    auto seed = std::random_device{}();
    auto gen  = rng::small_generator{seed};

    // Prepare all test data before benchmarking
    auto test_data = prepare_test_data<Buffer>(gen);

    bench.run("Decoding a uint", [&test_data]() {
        auto     dec = make_decoder(test_data.uint_data);
        uint64_t value;
        std::ignore = dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a uint with check", [&test_data]() {
        auto     dec = make_decoder(test_data.uint_data);
        uint64_t value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a int", [&test_data]() {
        auto    dec = make_decoder(test_data.int_data);
        int64_t value;
        std::ignore = dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a int with check", [&test_data]() {
        auto    dec = make_decoder(test_data.int_data);
        int64_t value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a bstr", [&test_data]() {
        auto                     dec = make_decoder(test_data.bstr_data);
        std::array<std::byte, 4> value;
        std::ignore = dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a bstr with check", [&test_data]() {
        auto                     dec = make_decoder(test_data.bstr_data);
        std::array<std::byte, 4> value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a tstr", [&test_data]() {
        auto        dec = make_decoder(test_data.tstr_data);
        std::string value;
        std::ignore = dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a tstr with check", [&test_data]() {
        auto        dec = make_decoder(test_data.tstr_data);
        std::string value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding an array", [&test_data]() {
        auto                   dec = make_decoder(test_data.array_data);
        std::array<int16_t, 4> value;
        std::ignore = dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding an array with check", [&test_data]() {
        auto                   dec = make_decoder(test_data.array_data);
        std::array<int16_t, 4> value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a map", [&test_data]() {
        auto                       dec = make_decoder(test_data.map_data);
        std::map<int16_t, int32_t> value;
        std::ignore = dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a map with check", [&test_data]() {
        auto                       dec = make_decoder(test_data.map_data);
        std::map<int16_t, int32_t> value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a tag", [&test_data]() {
        auto dec = make_decoder(test_data.tag_data);
        A    value;
        std::ignore = dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a tag with check", [&test_data]() {
        auto dec = make_decoder(test_data.tag_data);
        A    value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a nullptr", [&test_data]() {
        auto           dec = make_decoder(test_data.nullptr_data);
        std::nullptr_t value;
        std::ignore = dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a bool with check", [&test_data]() {
        auto dec = make_decoder(test_data.bool_data);
        bool value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a float16", [&test_data]() {
        auto      dec = make_decoder(test_data.float16_data);
        float16_t value;
        std::ignore = dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a float32", [&test_data]() {
        auto  dec = make_decoder(test_data.float32_data);
        float value;
        std::ignore = dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a float64", [&test_data]() {
        auto   dec = make_decoder(test_data.float64_data);
        double value;
        std::ignore = dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a float16_t with check", [&test_data]() {
        auto      dec = make_decoder(test_data.float16_data);
        float16_t value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a float32 with check", [&test_data]() {
        auto  dec = make_decoder(test_data.float32_data);
        float value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a double with check", [&test_data]() {
        auto   dec = make_decoder(test_data.float64_data);
        double value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Decoding a simple with check", [&test_data]() {
        auto   dec = make_decoder(test_data.simple_data);
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

// Function to run nanobench decoding benchmarks
template <typename Buffer> void run_decoding_benchmarks_roundtrip(ankerl::nanobench::Bench &bench) {
    auto seed = std::random_device{}();
    auto gen  = rng::small_generator{seed};

    bench.run("Encode+decode a uint", [&gen]() {
        // Prepare encoded data
        Buffer data;
        auto   enc      = make_encoder(data);
        auto   original = gen();
        std::ignore     = enc(original);

        // Decode and benchmark
        auto     dec = make_decoder(data);
        uint64_t value;
        std::ignore = dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a uint with check", [&gen]() {
        Buffer data;
        auto   enc      = make_encoder(data);
        auto   original = gen();
        std::ignore     = enc(original);

        auto     dec = make_decoder(data);
        uint64_t value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a int", [&gen]() {
        Buffer data;
        auto   enc      = make_encoder(data);
        auto   original = static_cast<int64_t>(gen());
        std::ignore     = enc(original);

        auto    dec = make_decoder(data);
        int64_t value;
        std::ignore = dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a int with check", [&gen]() {
        Buffer data;
        auto   enc      = make_encoder(data);
        auto   original = static_cast<int64_t>(gen());
        std::ignore     = enc(original);

        auto    dec = make_decoder(data);
        int64_t value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a bstr", [&gen]() {
        Buffer                   data;
        auto                     enc = make_encoder(data);
        std::array<std::byte, 4> original;
        for (auto &b : original) {
            b = std::byte(gen());
        }
        std::ignore = enc(original);

        auto                     dec = make_decoder(data);
        std::array<std::byte, 4> value;
        std::ignore = dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a bstr with check", [&gen]() {
        Buffer                   data;
        auto                     enc = make_encoder(data);
        std::array<std::byte, 4> original;
        for (auto &b : original) {
            b = std::byte(gen());
        }
        std::ignore = enc(original);

        auto                     dec = make_decoder(data);
        std::array<std::byte, 4> value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a tstr", [&gen]() {
        Buffer      data;
        auto        enc = make_encoder(data);
        std::string s;
        s.resize(4);
        std::transform(s.begin(), s.end(), s.begin(), [&gen](auto) { return static_cast<char>(gen.generate()); });
        std::ignore = enc(s);

        auto        dec = make_decoder(data);
        std::string value;
        std::ignore = dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a tstr with check", [&gen]() {
        Buffer      data;
        auto        enc = make_encoder(data);
        std::string s;
        s.resize(4);
        std::transform(s.begin(), s.end(), s.begin(), [&gen](auto) { return static_cast<char>(gen.generate()); });
        std::ignore = enc(s);

        auto        dec = make_decoder(data);
        std::string value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode an array", [&gen]() {
        Buffer                 data;
        auto                   enc = make_encoder(data);
        std::array<int16_t, 4> original{static_cast<short>(gen()), static_cast<short>(gen()), static_cast<short>(gen()),
                                        static_cast<short>(gen())};
        std::ignore = enc(original);

        auto                   dec = make_decoder(data);
        std::array<int16_t, 4> value;
        std::ignore = dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode an array with check", [&gen]() {
        Buffer                 data;
        auto                   enc = make_encoder(data);
        std::array<int16_t, 4> original{static_cast<short>(gen()), static_cast<short>(gen()), static_cast<short>(gen()),
                                        static_cast<short>(gen())};
        std::ignore = enc(original);

        auto                   dec = make_decoder(data);
        std::array<int16_t, 4> value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a map", [&gen]() {
        Buffer                     data;
        auto                       enc = make_encoder(data);
        std::map<int16_t, int32_t> original{{static_cast<short>(gen()), static_cast<int>(gen())},
                                            {static_cast<short>(gen()), static_cast<int>(gen())}};
        std::ignore = enc(original);

        auto                       dec = make_decoder(data);
        std::map<int16_t, int32_t> value;
        std::ignore = dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a map with check", [&gen]() {
        Buffer                     data;
        auto                       enc = make_encoder(data);
        std::map<int16_t, int32_t> original{{static_cast<short>(gen()), static_cast<int>(gen())},
                                            {static_cast<short>(gen()), static_cast<int>(gen())}};
        std::ignore = enc(original);

        auto                       dec = make_decoder(data);
        std::map<int16_t, int32_t> value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    struct A {
        static_tag<333> cbor_tag;
        int64_t         value;
    };

    bench.run("Encode+decode a tag", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        A      original{.cbor_tag = {}, .value = static_cast<int64_t>(gen())};
        std::ignore = enc(original);

        auto dec = make_decoder(data);
        A    value;
        std::ignore = dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a tag with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        A      original{.cbor_tag = {}, .value = static_cast<int64_t>(gen())};
        std::ignore = enc(original);

        auto dec = make_decoder(data);
        A    value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a nullptr", []() {
        Buffer data;
        auto   enc  = make_encoder(data);
        std::ignore = enc(std::nullptr_t{});

        auto           dec = make_decoder(data);
        std::nullptr_t value;
        std::ignore = dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a bool with check", [&gen]() {
        Buffer data;
        auto   enc      = make_encoder(data);
        bool   original = static_cast<bool>(gen() % 2);
        std::ignore     = enc(original);

        auto dec = make_decoder(data);
        bool value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a float16", [&gen]() {
        Buffer data;
        auto   enc      = make_encoder(data);
        auto   original = static_cast<float16_t>(static_cast<float>(gen()));
        std::ignore     = enc(original);

        auto      dec = make_decoder(data);
        float16_t value;
        std::ignore = dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a float32", [&gen]() {
        Buffer data;
        auto   enc      = make_encoder(data);
        auto   original = static_cast<float>(gen());
        std::ignore     = enc(original);

        auto  dec = make_decoder(data);
        float value;
        std::ignore = dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a float64", [&gen]() {
        Buffer data;
        auto   enc      = make_encoder(data);
        auto   original = static_cast<double>(gen());
        std::ignore     = enc(original);

        auto   dec = make_decoder(data);
        double value;
        std::ignore = dec(value);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a float16_t with check", [&gen]() {
        Buffer data;
        auto   enc      = make_encoder(data);
        auto   original = static_cast<float16_t>(static_cast<float>(gen()));
        std::ignore     = enc(original);

        auto      dec = make_decoder(data);
        float16_t value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a float32 with check", [&gen]() {
        Buffer data;
        auto   enc      = make_encoder(data);
        auto   original = static_cast<float>(gen());
        std::ignore     = enc(original);

        auto  dec = make_decoder(data);
        float value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a double with check", [&gen]() {
        Buffer data;
        auto   enc      = make_encoder(data);
        auto   original = static_cast<double>(gen());
        std::ignore     = enc(original);

        auto   dec = make_decoder(data);
        double value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a simple with check", [&gen]() {
        Buffer data;
        auto   enc = make_encoder(data);
        simple original{static_cast<simple::value_type>(gen())};
        std::ignore = enc(original);

        auto   dec = make_decoder(data);
        simple value;
        CHECK(dec(value));
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a variant(2) with check", [&gen]() {
        Buffer data;
        auto   enc    = make_encoder(data);
        using variant = std::variant<int, double>;
        auto i        = gen();
        if (i % 2 == 0) {
            std::ignore = enc(static_cast<double>(gen()));
        } else {
            std::ignore = enc(static_cast<int>(gen()));
        }

        auto    dec = make_decoder(data);
        variant value;
        auto    result = dec(value);
        if (!result) {
            fmt::print("Error: {}\n", status_message(result.error()));
        }
        CHECK(result);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a variant(3) with check", [&gen]() {
        Buffer data;
        auto   enc    = make_encoder(data);
        using variant = std::variant<int, double, std::string_view>;
        auto i        = gen();
        if (i % 3 == 0) {
            std::ignore = enc(std::string_view{"hello"});
        } else if (i % 3 == 1) {
            std::ignore = enc(static_cast<double>(gen()));
        } else {
            std::ignore = enc(static_cast<int>(gen()));
        }

        auto    dec = make_decoder(data);
        variant value;
        auto    result = dec(value);
        if (!result) {
            fmt::print("Error: {}\n", status_message(result.error()));
        }
        CHECK(result);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a variant(4) (1 tags) with check", [&gen]() {
        Buffer data;
        auto   enc    = make_encoder(data);
        using variant = std::variant<int, double, std::string_view, A>;
        auto i        = gen();
        if (i % 4 == 0) {
            std::ignore = enc(std::string_view{"hello"});
        } else if (i % 4 == 1) {
            std::ignore = enc(static_cast<double>(gen()));
        } else if (i % 4 == 2) {
            std::ignore = enc(A{.cbor_tag = {}, .value = static_cast<int64_t>(gen())});
        } else {
            std::ignore = enc(static_cast<int>(gen()));
        }

        auto    dec = make_decoder(data);
        variant value;
        auto    result = dec(value);
        if (!result) {
            fmt::print("Error: {}\n", status_message(result.error()));
        }
        CHECK(result);
        ankerl::nanobench::doNotOptimizeAway(value);
    });

    bench.run("Encode+decode a variant(4) (2 tags) with check", [&gen]() {
        Buffer data;
        auto   enc    = make_encoder(data);
        using variant = std::variant<int, double, B<100>, B<101>>;
        auto i        = gen();
        if (i % 4 == 0) {
            std::ignore = enc(B<100>{.cbor_tag = {}, .value = static_cast<int64_t>(gen())});
        } else if (i % 4 == 1) {
            std::ignore = enc(static_cast<double>(gen()));
        } else if (i % 4 == 2) {
            std::ignore = enc(B<101>{.cbor_tag = {}, .value = static_cast<int64_t>(gen())});
        } else {
            std::ignore = enc(static_cast<int>(gen()));
        }

        auto    dec = make_decoder(data);
        variant value;
        auto    result = dec(value);
        if (!result) {
            fmt::print("Error: {}\n", status_message(result.error()));
        }
        CHECK(result);
        ankerl::nanobench::doNotOptimizeAway(value);
    });
}

template <typename ContainerType> void run_decoding_benchmark_roundtrip() {
    ankerl::nanobench::Bench bench;
    benchmark_options        options;

    bench.title(std::string(nameof::nameof_type<ContainerType>()) + " buffer (encode+decode roundtrip)");
    bench.minEpochIterations(100);
    bench.unit(std::string(options.unit));
    bench.performanceCounters(true);
    bench.relative(options.relative);

    run_decoding_benchmarks_roundtrip<ContainerType>(bench);
}

template <std::size_t N> void run_encoded_view_benchmark() {
    std::array<std::uint32_t, N> input{};
    for (std::size_t index = 0; index < input.size(); ++index) {
        input[index] = static_cast<std::uint32_t>(index);
    }

    std::vector<std::uint8_t> encoded;
    auto                      enc = make_encoder(encoded);
    REQUIRE(enc(input));

    ankerl::nanobench::Bench bench;
    bench.title(fmt::format("Encoded-view decode, {} fixed-array elements", N));
    bench.minEpochIterations(1000);
    bench.unit("byte");
    bench.batch(encoded.size());
    bench.performanceCounters(true);
    bench.relative(true);

    bench.run("Typed decode", [&encoded] {
        std::array<std::uint32_t, N> output;
        auto                         dec    = make_decoder(encoded);
        auto                         result = dec(output);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(output);
    });

    bench.run("Typed decode with encoded view", [&encoded] {
        std::array<std::uint32_t, N> output;
        auto                         dec    = make_decoder_with_options<encoded_item_view_decoder_options>(encoded);
        auto                         result = dec(output);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(output);
    });

    bench.run("Raw encoded item view", [&encoded] {
        auto                                          dec = make_decoder(encoded);
        typename decltype(dec)::raw_encoded_item_view output;
        auto                                          result = dec(output);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(output);
    });
}

TEST_CASE("Encoded view decoder benchmarks") {
    run_encoded_view_benchmark<4>();
    run_encoded_view_benchmark<4096>();
}

TEST_CASE("Decoding benchmarks") {
    run_decoding_benchmark<std::vector<uint8_t>>();
    run_decoding_benchmark<std::deque<uint8_t>>();
    run_decoding_benchmark<std::array<uint8_t, 20>>();

    run_decoding_benchmark_roundtrip<std::vector<uint8_t>>();
    // run_decoding_benchmark_roundtrip<std::deque<uint8_t>>(); // String_view in test, cannot decode on a deque
    run_decoding_benchmark_roundtrip<std::array<uint8_t, 20>>();
}

namespace {

std::vector<std::byte> make_definite_zero_array(std::size_t count) {
    std::vector<std::uint64_t> values(count, 0U);
    std::vector<std::byte>     encoded;
    auto                       encoder = make_encoder(encoded);
    REQUIRE(encoder(values));
    return encoded;
}

std::vector<std::byte> make_indefinite_zero_array(std::size_t count) {
    std::vector<std::byte> encoded;
    encoded.reserve(count + 2U);
    encoded.push_back(std::byte{0x9F});
    encoded.insert(encoded.end(), count, std::byte{0x00});
    encoded.push_back(std::byte{0xFF});
    return encoded;
}

void benchmark_bounded_array_decode(std::string_view title, const std::vector<std::byte> &encoded, std::size_t count) {
    std::vector<std::uint64_t> direct_value;
    std::vector<std::uint64_t> bounded_value;
    std::vector<std::uint64_t> dynamic_bounded_value;
    direct_value.reserve(count);
    bounded_value.reserve(count);
    dynamic_bounded_value.reserve(count);

    {
        auto decoder = make_decoder(encoded);
        REQUIRE(decoder(direct_value));
    }
    {
        auto decoder = make_decoder(encoded);
        REQUIRE(decoder(as_bounded_size<0, 4096>(bounded_value)));
    }
    {
        auto decoder = make_decoder(encoded);
        REQUIRE(decoder(as_bounded_size(dynamic_bounded_value, 0, count)));
    }
    REQUIRE(direct_value == bounded_value);
    REQUIRE(direct_value == dynamic_bounded_value);

    ankerl::nanobench::Bench bench;
    bench.title(std::string{title});
    bench.unit("byte");
    bench.relative(true);
    bench.performanceCounters(false);
    bench.epochs(15);
    bench.minEpochTime(std::chrono::milliseconds{20});
    bench.minEpochIterations(20);

    bench.batch(encoded.size()).run("direct typed decode", [&] {
        direct_value.clear();
        auto decoder = make_decoder(encoded);
        auto result  = decoder(direct_value);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(direct_value.data());
    });

    bench.batch(encoded.size()).run("bounded typed decode", [&] {
        bounded_value.clear();
        auto decoder = make_decoder(encoded);
        auto result  = decoder(as_bounded_size<0, 4096>(bounded_value));
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(bounded_value.data());
    });

    bench.batch(encoded.size()).run("dynamic bounded typed decode", [&] {
        dynamic_bounded_value.clear();
        auto decoder = make_decoder(encoded);
        auto result  = decoder(as_bounded_size(dynamic_bounded_value, 0, count));
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(dynamic_bounded_value.data());
    });
}

} // namespace

TEST_CASE("Bounded decode traversal benchmarks") {
    constexpr std::size_t count = 4096U;
    benchmark_bounded_array_decode("definite uint array, 4096 elements", make_definite_zero_array(count), count);
    benchmark_bounded_array_decode("indefinite uint array, 4096 elements", make_indefinite_zero_array(count), count);
}

// Main function to run tests and benchmarks
int main(int argc, char **argv) {
    // Run Doctest tests
    int result = doctest::Context(argc, argv).run();
    return result;
}
