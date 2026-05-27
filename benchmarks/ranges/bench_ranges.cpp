#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"
#include "test_util.h"

#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_lazy_tags.h>
#include <cbor_tags/cbor_ranges.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <nanobench.h>
#include <numeric>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

using namespace cbor::tags;

namespace {

constexpr std::size_t item_count = 256;

struct benchmark_options {
    std::string_view unit{"Ops"};
    bool             relative{true};
};

std::vector<int> make_values() {
    std::vector<int> values(item_count);
    std::iota(values.begin(), values.end(), 0);
    return values;
}

std::vector<std::pair<int, int>> make_pairs() {
    std::vector<std::pair<int, int>> pairs;
    pairs.reserve(item_count);
    for (int value = 0; value < static_cast<int>(item_count); ++value) {
        pairs.emplace_back(value, value * 2);
    }
    return pairs;
}

std::vector<std::byte> make_bytes() {
    std::vector<std::byte> bytes(item_count);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::byte>(index);
    }
    return bytes;
}

std::vector<std::byte> make_bytes(std::size_t size) {
    std::vector<std::byte> bytes(size);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::byte>(index % 251U);
    }
    return bytes;
}

std::string make_text(std::size_t size) {
    std::string text(size, '\0');
    for (std::size_t index = 0; index < text.size(); ++index) {
        text[index] = static_cast<char>('a' + (index % 26U));
    }
    return text;
}

template <typename Value> std::vector<std::byte> encode_to_vector(Value &&value) {
    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    CHECK(enc(std::forward<Value>(value)));
    return buffer;
}

template <typename Value> void run_encode(Value &&value) {
    std::vector<std::byte> buffer;
    buffer.reserve(item_count * 8);
    auto enc    = make_encoder(buffer);
    std::ignore = enc(std::forward<Value>(value));
    ankerl::nanobench::doNotOptimizeAway(buffer);
}

void configure(ankerl::nanobench::Bench &bench, std::string_view title) {
    benchmark_options options;
    bench.title(std::string{title});
    bench.minEpochIterations(50);
    bench.relative(options.relative);
    bench.unit(options.unit.data());
    bench.performanceCounters(true);
}

void configure_throughput(ankerl::nanobench::Bench &bench, std::string_view title) {
    bench.title(std::string{title});
    bench.minEpochIterations(20);
    bench.relative(true);
    bench.unit("byte");
    bench.performanceCounters(false);
}

} // namespace

TEST_CASE("Range encoding benchmarks") {
    auto values = make_values();
    auto pairs  = make_pairs();
    auto bytes  = make_bytes();

    auto iota_values       = std::views::iota(0, static_cast<int>(item_count));
    auto filtered_values   = values | std::views::filter([](int) { return true; });
    auto transformed_pairs = values | std::views::transform([](int value) { return std::pair{value, value * 2}; });
    auto filtered_pairs    = values | std::views::filter([](int) { return true; }) |
                          std::views::transform([](int value) { return std::pair{value, value * 2}; });
    auto transformed_bytes = values | std::views::transform([](int value) { return static_cast<std::uint8_t>(value); });
    auto chunked_bytes     = bytes | std::views::filter([](std::byte) { return true; });

    std::map<int, int> ordered_pairs(pairs.begin(), pairs.end());

    ankerl::nanobench::Bench bench;
    configure(bench, "range encoding");

    bench.run("direct vector<int> array", [&] { run_encode(values); });
    bench.run("as_array_range(vector<int>) sized", [&] { run_encode(as_array_range(values)); });
    bench.run("as_array_range(iota_view) sized", [&] { run_encode(as_array_range(iota_values)); });
    bench.run("as_array_range(filter_view) indefinite", [&] { run_encode(as_array_range(filtered_values)); });

    bench.run("direct std::map<int,int>", [&] { run_encode(ordered_pairs); });
    bench.run("as_map_range(vector<pair>) sized", [&] { run_encode(as_map_range(pairs)); });
    bench.run("as_map_range(transform_view) sized", [&] { run_encode(as_map_range(transformed_pairs)); });
    bench.run("as_map_range(filter_transform_view) indefinite", [&] { run_encode(as_map_range(filtered_pairs)); });

    bench.run("direct vector<byte> bstr", [&] { run_encode(bytes); });
    bench.run("as_bstr_range(vector<byte>) sized", [&] { run_encode(as_bstr_range(bytes)); });
    bench.run("as_bstr_range(transform_view) sized", [&] { run_encode(as_bstr_range(transformed_bytes)); });
    bench.run("as_bstr_range(filter_view) chunked", [&] { run_encode(as_bstr_range(chunked_bytes, 64)); });
}

TEST_CASE("Range decoding benchmarks") {
    auto values = make_values();
    auto pairs  = make_pairs();
    auto bytes  = make_bytes();

    auto filtered_values = values | std::views::filter([](int) { return true; });
    auto filtered_pairs  = values | std::views::filter([](int) { return true; }) |
                          std::views::transform([](int value) { return std::pair{value, value * 2}; });
    auto chunked_bytes = bytes | std::views::filter([](std::byte) { return true; });

    auto definite_array    = encode_to_vector(as_array_range(values));
    auto indef_array       = encode_to_vector(as_array_range(filtered_values));
    auto definite_map      = encode_to_vector(as_map_range(pairs));
    auto indef_map         = encode_to_vector(as_map_range(filtered_pairs));
    auto definite_bstr     = encode_to_vector(as_bstr_range(bytes));
    auto indef_bstr        = encode_to_vector(as_bstr_range(chunked_bytes, 64));
    auto deque_indef_array = std::deque<std::byte>(indef_array.begin(), indef_array.end());

    ankerl::nanobench::Bench bench;
    configure(bench, "range decoding");

    bench.run("definite array from vector buffer", [&] {
        std::vector<int> decoded;
        auto             dec = make_decoder(definite_array);
        std::ignore          = dec(decoded);
        ankerl::nanobench::doNotOptimizeAway(decoded);
    });

    bench.run("indefinite array from vector buffer", [&] {
        std::vector<int> decoded;
        auto             dec = make_decoder(indef_array);
        std::ignore          = dec(decoded);
        ankerl::nanobench::doNotOptimizeAway(decoded);
    });

    bench.run("indefinite array from deque buffer", [&] {
        std::vector<int> decoded;
        auto             dec = make_decoder(deque_indef_array);
        std::ignore          = dec(decoded);
        ankerl::nanobench::doNotOptimizeAway(decoded);
    });

    bench.run("definite map from vector buffer", [&] {
        std::map<int, int> decoded;
        auto               dec = make_decoder(definite_map);
        std::ignore            = dec(decoded);
        ankerl::nanobench::doNotOptimizeAway(decoded);
    });

    bench.run("indefinite map from vector buffer", [&] {
        std::map<int, int> decoded;
        auto               dec = make_decoder(indef_map);
        std::ignore            = dec(decoded);
        ankerl::nanobench::doNotOptimizeAway(decoded);
    });

    bench.run("definite bstr from vector buffer", [&] {
        std::vector<std::byte> decoded;
        auto                   dec = make_decoder(definite_bstr);
        std::ignore                = dec(decoded);
        ankerl::nanobench::doNotOptimizeAway(decoded);
    });

    bench.run("indefinite bstr from vector buffer", [&] {
        std::vector<std::byte> decoded;
        auto                   dec = make_decoder(indef_bstr);
        std::ignore                = dec(decoded);
        ankerl::nanobench::doNotOptimizeAway(decoded);
    });
}

TEST_CASE("String payload throughput fixtures roundtrip") {
    constexpr auto payload_sizes = std::array<std::size_t, 3>{256U, 4096U, 65536U};

    for (auto size : payload_sizes) {
        auto bytes = make_bytes(size);
        auto text  = make_text(size);

        auto bstr_encoded = encode_to_vector(bytes);
        auto tstr_encoded = encode_to_vector(text);

        {
            std::vector<std::byte> decoded;
            auto                   dec = make_decoder(bstr_encoded);
            CHECK(dec(decoded));
            CHECK(decoded == bytes);
        }
        {
            std::span<const std::byte> decoded;
            auto                       dec = make_decoder(bstr_encoded);
            CHECK(dec(decoded));
            CHECK(decoded.size() == bytes.size());
            CHECK(std::equal(decoded.begin(), decoded.end(), bytes.begin(), bytes.end()));
        }
        {
            std::string decoded;
            auto        dec = make_decoder(tstr_encoded);
            CHECK(dec(decoded));
            CHECK(decoded == text);
        }
        {
            std::string_view decoded;
            auto             dec = make_decoder(tstr_encoded);
            CHECK(dec(decoded));
            CHECK(decoded == text);
        }
    }
}

TEST_CASE("String payload throughput benchmarks") {
    constexpr auto payload_sizes = std::array<std::size_t, 3>{256U, 4096U, 65536U};

    ankerl::nanobench::Bench bench;
    configure_throughput(bench, "bstr/tstr throughput");

    for (auto size : payload_sizes) {
        auto bytes = make_bytes(size);
        auto text  = make_text(size);

        auto bstr_encoded = encode_to_vector(bytes);
        auto tstr_encoded = encode_to_vector(text);

        {
            std::vector<std::byte> decoded;
            auto                   dec = make_decoder(bstr_encoded);
            CHECK(dec(decoded));
            CHECK(decoded == bytes);
        }
        {
            std::span<const std::byte> decoded;
            auto                       dec = make_decoder(bstr_encoded);
            CHECK(dec(decoded));
            CHECK(decoded.size() == bytes.size());
            CHECK(std::equal(decoded.begin(), decoded.end(), bytes.begin(), bytes.end()));
        }
        {
            std::string decoded;
            auto        dec = make_decoder(tstr_encoded);
            CHECK(dec(decoded));
            CHECK(decoded == text);
        }
        {
            std::string_view decoded;
            auto             dec = make_decoder(tstr_encoded);
            CHECK(dec(decoded));
            CHECK(decoded == text);
        }

        std::vector<std::byte> bstr_encode_buffer;
        bstr_encode_buffer.reserve(bstr_encoded.size());
        std::vector<std::byte> tstr_encode_buffer;
        tstr_encode_buffer.reserve(tstr_encoded.size());

        auto const bstr_encode_name = std::string{"bstr["} + std::to_string(size) + "] encode reused buffer";
        bench.batch(bstr_encoded.size()).run(bstr_encode_name, [&] {
            bstr_encode_buffer.clear();
            auto enc    = make_encoder(bstr_encode_buffer);
            std::ignore = enc(bytes);
            ankerl::nanobench::doNotOptimizeAway(bstr_encode_buffer);
        });

        auto const tstr_encode_name = std::string{"tstr["} + std::to_string(size) + "] encode reused buffer";
        bench.batch(tstr_encoded.size()).run(tstr_encode_name, [&] {
            tstr_encode_buffer.clear();
            auto enc    = make_encoder(tstr_encode_buffer);
            std::ignore = enc(text);
            ankerl::nanobench::doNotOptimizeAway(tstr_encode_buffer);
        });

        auto const bstr_decode_name = std::string{"bstr["} + std::to_string(size) + "] decode owning vector<byte>";
        bench.batch(bstr_encoded.size()).run(bstr_decode_name, [&] {
            std::vector<std::byte> decoded;
            auto                   dec = make_decoder(bstr_encoded);
            std::ignore                = dec(decoded);
            ankerl::nanobench::doNotOptimizeAway(decoded);
        });

        auto const bstr_view_decode_name = std::string{"bstr["} + std::to_string(size) + "] decode borrowed span bind (represented bytes)";
        bench.batch(bstr_encoded.size()).run(bstr_view_decode_name, [&] {
            std::span<const std::byte> decoded;
            auto                       dec = make_decoder(bstr_encoded);
            std::ignore                    = dec(decoded);
            ankerl::nanobench::doNotOptimizeAway(decoded);
        });

        auto const tstr_decode_name = std::string{"tstr["} + std::to_string(size) + "] decode owning string";
        bench.batch(tstr_encoded.size()).run(tstr_decode_name, [&] {
            std::string decoded;
            auto        dec = make_decoder(tstr_encoded);
            std::ignore     = dec(decoded);
            ankerl::nanobench::doNotOptimizeAway(decoded);
        });

        auto const tstr_view_decode_name =
            std::string{"tstr["} + std::to_string(size) + "] decode borrowed string_view bind (represented bytes)";
        bench.batch(tstr_encoded.size()).run(tstr_view_decode_name, [&] {
            std::string_view decoded;
            auto             dec = make_decoder(tstr_encoded);
            std::ignore          = dec(decoded);
            ankerl::nanobench::doNotOptimizeAway(decoded);
        });
    }
}

TEST_CASE("Lazy tag range benchmarks") {
    auto values = make_values();
    auto pairs  = make_pairs();
    auto bytes  = make_bytes();

    std::vector<std::byte> buffer;
    auto                   enc = make_encoder(buffer);
    CHECK(enc(as_array{4}, make_tag_pair(static_tag<100>{}, as_array_range(values)), make_tag_pair(static_tag<200>{}, as_map_range(pairs)),
              make_tag_pair(static_tag<300>{}, as_bstr_range(bytes)), make_tag_pair(static_tag<400>{}, 42)));

    std::deque<std::byte> deque_buffer(buffer.begin(), buffer.end());

    ankerl::nanobench::Bench bench;
    configure(bench, "lazy tag ranges");

    bench.run("find tag in vector buffer", [&] {
        auto view = find_tags<400>(buffer);
        auto it   = view.begin();
        ankerl::nanobench::doNotOptimizeAway(it);
    });

    bench.run("find tag in deque buffer", [&] {
        auto view = find_tags<400>(deque_buffer);
        auto it   = view.begin();
        ankerl::nanobench::doNotOptimizeAway(it);
    });

    bench.run("find and decode array payload", [&] {
        auto             view = find_tags<100>(buffer);
        auto             it   = view.begin();
        std::vector<int> decoded;
        if (it != view.end()) {
            std::ignore = it->decode(decoded);
        }
        ankerl::nanobench::doNotOptimizeAway(decoded);
    });
}

int main(int argc, char **argv) { return doctest::Context(argc, argv).run(); }
