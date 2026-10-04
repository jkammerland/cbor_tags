#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <chrono>
#include <cstddef>
#include <doctest/doctest.h>
#include <nanobench.h>
#include <optional>
#include <string>
#include <vector>

namespace {

using namespace cbor::tags;

void benchmark_text_destinations(std::size_t size) {
    std::string source(size, 'x');
    for (std::size_t i = 0; i < source.size(); ++i) {
        source[i] = static_cast<char>('a' + i % 26U);
    }
    std::vector<std::byte> encoded;
    auto                   encoder = make_encoder(encoded);
    REQUIRE(encoder(source));

    std::string reused;
    reused.reserve(source.size());
    auto check_decoder = make_decoder(encoded);
    REQUIRE(check_decoder(reused));
    REQUIRE(reused == source);
    REQUIRE(check_decoder.tell() == encoded.end());
    reused.clear();

    std::optional<std::string> fresh;
    bool                       fresh_succeeded  = true;
    bool                       reused_succeeded = true;

    ankerl::nanobench::Bench bench;
    bench.title("text decode, " + std::to_string(size) + " payload bytes");
    bench.unit("byte").batch(source.size()).relative(true).performanceCounters(false);
    bench.epochs(15).minEpochTime(std::chrono::milliseconds{20}).minEpochIterations(20);

    bench.run("fresh destination", [&] {
        // emplace destroys the preceding string, so this decode starts with no retained storage.
        fresh.emplace();
        auto decoder = make_decoder(encoded);
        auto result  = decoder(*fresh);
        fresh_succeeded &= static_cast<bool>(result);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(*fresh);
    });
    REQUIRE(fresh_succeeded);
    REQUIRE(fresh.has_value());
    CHECK(*fresh == source);

    bench.run("cleared reserved destination", [&] {
        reused.clear();
        auto decoder = make_decoder(encoded);
        auto result  = decoder(reused);
        reused_succeeded &= static_cast<bool>(result);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(reused);
    });
    REQUIRE(reused_succeeded);
    CHECK(reused == source);
}

} // namespace

// Run with bench_decoder --test-case="text destination reuse benchmarks".
TEST_CASE("text destination reuse benchmarks") {
    benchmark_text_destinations(4U);
    benchmark_text_destinations(128U);
    benchmark_text_destinations(4096U);
    benchmark_text_destinations(65536U);
}
