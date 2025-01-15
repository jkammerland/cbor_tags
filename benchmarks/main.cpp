#include "cbor_tags/cbor.h"
#include "small_generator.h"

#include <algorithm>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_get_random_seed.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fmt/base.h>
#include <iostream>
#include <list>
#include <memory_resource>
#include <random>

using namespace cbor::tags;

int basic_array_test(auto seed) {
    auto buffer = std::vector<std::byte>{};
    auto enc    = make_encoder(buffer);

    rng::small_generator       gen(seed);
    std::array<uint64_t, 1000> input;
    std::ranges::generate(input, [&gen] { return gen(); });
    REQUIRE(enc(input));

    std::vector<uint64_t> output;
    auto                  dec    = make_decoder(buffer);
    auto                  status = dec(output);
    if (!status) {
        INFO("Error: " << cbor::tags::status_message(status.error()));
    }
    REQUIRE(status);
    REQUIRE(std::ranges::equal(input, output));
    return 0;
}

enum class roles : std::uint8_t { admin, user, guest };

namespace v1 {
struct UserProfile {
    static constexpr std::uint64_t cbor_tag = 140; // Inline tag
    std::string                    name;
    int64_t                        age;
};
} // namespace v1

namespace v2 {
struct UserProfile {
    static constexpr std::uint64_t cbor_tag = 141; // Inline tag
    std::string                    name;
    int64_t                        age;
    roles                          role;
};
} // namespace v2

TEST_CASE("basic_variant_test") {
    // Encoding
    using namespace cbor::tags;
    auto data          = std::vector<std::byte>{};
    auto enc           = make_encoder(data);
    auto result_encode = enc(v2::UserProfile{.name = "John Doe", .age = 30, .role = roles::admin});
    if (!result_encode) {
        std::cerr << "Encoding status: " << status_message(result_encode.error()) << std::endl;
    }
    // ...

    // Decoding - supporting multiple versions
    using variant = std::variant<v1::UserProfile, v2::UserProfile>;
    using namespace cbor::tags;
    auto    dec = make_decoder(data);
    variant user;
    auto    result_decode = dec(user);
    if (!result_decode) {
        std::cerr << "Decoding status: " << status_message(result_decode.error()) << std::endl;
    }
    // should now hold a v2::UserProfile
    // ...

    REQUIRE(std::holds_alternative<v2::UserProfile>(user));
    CHECK(std::get<v2::UserProfile>(user).name == "John Doe");
    CHECK(std::get<v2::UserProfile>(user).age == 30);
}

TEST_CASE("decoder benchmarks", "[decoder]") {
    auto seed = Catch::getSeed();
    INFO("Random seed: " << seed);
    basic_array_test(seed);

    auto buffer = std::vector<std::byte>{};
    auto enc    = make_encoder(buffer);

    rng::small_generator    gen(seed);
    constexpr uint64_t      N = 10000;
    std::array<uint64_t, N> input;
    std::ranges::generate(input, [&gen] { return gen(); });
    REQUIRE(enc(input));

    BENCHMARK_ADVANCED("Bench array decoding")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&buffer]() mutable {
            std::array<uint64_t, N> output_array;
            auto                    dec    = make_decoder(buffer);
            auto                    status = dec(output_array);
            CHECK(status);
            return output_array;
        });
    };

    BENCHMARK_ADVANCED("Bench vector decoding")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&buffer]() mutable {
            std::vector<uint64_t> output_array;
            auto                  dec    = make_decoder(buffer);
            auto                  status = dec(output_array);
            CHECK(status);
            return output_array;
        });
    };

    BENCHMARK_ADVANCED("Bench pmr::vector decoding")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&buffer]() mutable {
            std::array<std::byte, N * sizeof(uint64_t)> R;
            std::pmr::monotonic_buffer_resource         resource(R.data(), R.size(), std::pmr::null_memory_resource());
            std::pmr::vector<uint64_t>                  output_array(&resource);
            auto                                        dec    = make_decoder(buffer);
            auto                                        status = dec(output_array);
            CHECK(status);
            return output_array;
        });
    };

    BENCHMARK_ADVANCED("Bench deque decoding")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&buffer]() mutable {
            std::deque<uint64_t> output_array;
            auto                 dec    = make_decoder(buffer);
            auto                 status = dec(output_array);
            CHECK(status);
            return output_array;
        });
    };

    BENCHMARK_ADVANCED("Bench list decoding")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&buffer]() mutable {
            std::list<uint64_t> output_array;
            auto                dec    = make_decoder(buffer);
            auto                status = dec(output_array);
            CHECK(status);
            return output_array;
        });
    };
}

TEST_CASE("Encoder benchmarks", "[encoder]") {
    auto seed = Catch::getSeed();
    INFO("Random seed: " << seed);
    basic_array_test(seed);

    constexpr uint64_t N = 10000;

    std::array<uint64_t, N> input;
    rng::small_generator    gen(seed);
    std::ranges::generate(input, [&gen] { return gen(); });

    BENCHMARK_ADVANCED("Bench array encoding")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&input]() {
            std::array<std::byte, 2 * N * sizeof(uint64_t)> buffer;
            auto                                            enc    = make_encoder(buffer);
            auto                                            status = enc(input);
            CHECK(status);
            return buffer;
        });
    };

    BENCHMARK_ADVANCED("Bench vector encoding")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&input]() {
            std::vector<std::byte> buffer;
            buffer.reserve(10 * N * sizeof(uint64_t));
            auto enc    = make_encoder(buffer);
            auto status = enc(input);
            CHECK(status);
            return buffer;
        });
    };

    BENCHMARK_ADVANCED("Bench pmr::vector encoding")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&input]() {
            std::array<std::byte, 10 * N * sizeof(uint64_t)> R;
            std::pmr::monotonic_buffer_resource              resource(R.data(), R.size(), std::pmr::null_memory_resource());
            std::pmr::vector<std::byte>                      buffer(&resource);
            auto                                             enc    = make_encoder(buffer);
            auto                                             status = enc(input);
            // fmt::print("Status: {}\n", status_message(status.error()));
            CHECK(status);
            return buffer;
        });
    };

    BENCHMARK_ADVANCED("Bench deque encoding")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&input]() {
            std::deque<std::byte> buffer;
            auto                  enc    = make_encoder(buffer);
            auto                  status = enc(input);
            CHECK(status);
            return buffer;
        });
    };

    BENCHMARK_ADVANCED("Bench list encoding")(Catch::Benchmark::Chronometer meter) {
        meter.measure([&input]() {
            std::list<std::byte> buffer;
            auto                 enc    = make_encoder(buffer);
            auto                 status = enc(input);
            CHECK(status);
            return buffer;
        });
    };
}
