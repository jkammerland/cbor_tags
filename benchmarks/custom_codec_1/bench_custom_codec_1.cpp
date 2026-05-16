#define DOCTEST_CONFIG_IMPLEMENT

#include "doctest/doctest.h"

#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/custom_codec_1.h>
#include <cstddef>
#include <cstdint>
#include <nanobench.h>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

namespace cc1 = cbor::tags::ext::custom_codec_1;

using byte_buffer = std::vector<std::byte>;

constexpr std::uint64_t record_tag = 60001U;
constexpr std::uint64_t vector_tag = 60002U;

struct tagged_record {
    static constexpr std::uint64_t cbor_tag = record_tag;

    std::uint32_t             id{};
    std::int64_t              sequence{};
    bool                      active{};
    float                     ratio{};
    double                    score{};
    std::array<std::byte, 16> token{};
    std::string               label{};
    std::vector<std::int16_t> samples{};

    friend auto operator==(tagged_record const &, tagged_record const &) -> bool = default;
};

auto make_record() -> tagged_record {
    return tagged_record{
        .id       = 0x12345678U,
        .sequence = 0x0102030405060708LL,
        .active   = true,
        .ratio    = 3.25F,
        .score    = 42.75,
        .token =
            std::array<std::byte, 16>{
                std::byte{0x10},
                std::byte{0x21},
                std::byte{0x32},
                std::byte{0x43},
                std::byte{0x54},
                std::byte{0x65},
                std::byte{0x76},
                std::byte{0x87},
                std::byte{0x98},
                std::byte{0xA9},
                std::byte{0xBA},
                std::byte{0xCB},
                std::byte{0xDC},
                std::byte{0xED},
                std::byte{0xFE},
                std::byte{0x0F},
            },
        .label   = "fixed telemetry frame",
        .samples = {4096, 4353, 4610, 4867, 5124, 5381, 5638, 5895, 6152, 6409, 6666, 6923},
    };
}

auto make_vector_values() -> std::vector<double> {
    return {
        1024.125,  2048.25,    4096.5,      8192.75,    16384.875, 32768.125,  65536.25,     131072.5,
        262144.75, 524288.875, 1048576.125, 2097152.25, 4194304.5, 8388608.75, 16777216.875, 33554432.125,
    };
}

auto make_vector_values(std::size_t count) -> std::vector<double> {
    if (count == 16U) {
        return make_vector_values();
    }

    std::vector<double> values;
    values.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        values.push_back(static_cast<double>(index + 1U) * 1024.0 + static_cast<double>((index % 7U) + 1U) * 0.125);
    }
    return values;
}

void configure_bench(ankerl::nanobench::Bench &bench, std::string_view title) {
    bench.title(std::string{title});
    bench.unit("Ops");
    bench.relative(true);
    bench.performanceCounters(true);
    bench.minEpochIterations(100);
}

void configure_throughput_bench(ankerl::nanobench::Bench &bench, std::string_view title) {
    bench.title(std::string{title});
    bench.unit("byte");
    bench.relative(true);
    bench.performanceCounters(false);
    bench.minEpochIterations(20);
}

template <typename Value> auto encode_default(Value const &value) -> byte_buffer {
    byte_buffer encoded;
    auto        encoder = cbor::tags::make_encoder(encoded);
    auto        result  = encoder(value);
    CHECK(result);
    return encoded;
}

template <typename Value> auto encode_custom(Value const &value) -> byte_buffer {
    byte_buffer encoded;
    auto        encoder = cbor::tags::make_encoder<cc1::custom_codec_1>(encoded);
    auto        result  = encoder(cc1::as_custom_codec_1(value));
    CHECK(result);
    return encoded;
}

template <std::uint64_t Tag, typename Value> auto encode_default_tagged(Value const &value) -> byte_buffer {
    byte_buffer encoded;
    auto        encoder = cbor::tags::make_encoder(encoded);
    auto        tagged  = cbor::tags::make_tag_pair(cbor::tags::static_tag<Tag>{}, value);
    auto        result  = encoder(tagged);
    CHECK(result);
    return encoded;
}

template <std::uint64_t Tag, typename Value> auto encode_custom_tagged(Value const &value) -> byte_buffer {
    byte_buffer encoded;
    auto        encoder = cbor::tags::make_encoder<cc1::custom_codec_1>(encoded);
    auto        result  = encoder(cc1::as_custom_codec_1(cbor::tags::static_tag<Tag>{}, value));
    CHECK(result);
    return encoded;
}

template <typename Value> auto decode_default(byte_buffer const &encoded) -> Value {
    Value value{};
    auto  decoder = cbor::tags::make_decoder(encoded);
    auto  result  = decoder(value);
    CHECK(result);
    return value;
}

template <typename Value> auto decode_custom(byte_buffer const &encoded) -> Value {
    Value value{};
    auto  decoder = cbor::tags::make_decoder<cc1::custom_codec_1>(encoded);
    auto  result  = decoder(cc1::as_custom_codec_1(value));
    CHECK(result);
    return value;
}

template <std::uint64_t Tag, typename Value> auto decode_default_tagged(byte_buffer const &encoded) -> Value {
    Value value{};
    auto  decoder = cbor::tags::make_decoder(encoded);
    auto  tagged  = cbor::tags::make_tag_pair(cbor::tags::static_tag<Tag>{}, value);
    auto  result  = decoder(tagged);
    CHECK(result);
    return value;
}

template <std::uint64_t Tag, typename Value> auto decode_custom_tagged(byte_buffer const &encoded) -> Value {
    Value value{};
    auto  decoder = cbor::tags::make_decoder<cc1::custom_codec_1>(encoded);
    auto  result  = decoder(cc1::as_custom_codec_1(cbor::tags::static_tag<Tag>{}, value));
    CHECK(result);
    return value;
}

} // namespace

TEST_CASE("custom_codec_1 comparison fixtures roundtrip") {
    auto const record        = make_record();
    auto const vector_values = make_vector_values();

    auto const default_record = encode_default(record);
    auto const custom_record  = encode_custom(record);
    auto const default_vector = encode_default_tagged<vector_tag>(vector_values);
    auto const custom_vector  = encode_custom_tagged<vector_tag>(vector_values);

    CHECK(decode_default<tagged_record>(default_record) == record);
    CHECK(decode_custom<tagged_record>(custom_record) == record);
    CHECK(decode_default_tagged<vector_tag, std::vector<double>>(default_vector) == vector_values);
    CHECK(decode_custom_tagged<vector_tag, std::vector<double>>(custom_vector) == vector_values);

    CAPTURE(default_record.size());
    CAPTURE(custom_record.size());
    CAPTURE(default_vector.size());
    CAPTURE(custom_vector.size());

    CHECK(custom_record.size() < default_record.size());
    CHECK(custom_vector.size() < default_vector.size());
}

TEST_CASE("custom_codec_1 encode comparison benchmarks") {
    auto const record        = make_record();
    auto const vector_values = make_vector_values();

    auto const default_record = encode_default(record);
    auto const custom_record  = encode_custom(record);
    auto const default_vector = encode_default_tagged<vector_tag>(vector_values);
    auto const custom_vector  = encode_custom_tagged<vector_tag>(vector_values);

    ankerl::nanobench::Bench bench;
    configure_bench(bench, "custom_codec_1 encode vs default CBOR");

    bench.run("default tagged record encode", [&] {
        byte_buffer encoded;
        encoded.reserve(default_record.size());
        auto encoder = cbor::tags::make_encoder(encoded);
        auto result  = encoder(record);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(encoded);
    });

    bench.run("custom_codec_1 tagged record encode", [&] {
        byte_buffer encoded;
        encoded.reserve(custom_record.size());
        auto encoder = cbor::tags::make_encoder<cc1::custom_codec_1>(encoded);
        auto result  = encoder(cc1::as_custom_codec_1(record));
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(encoded);
    });

    bench.run("default tagged vector<double> encode", [&] {
        byte_buffer encoded;
        encoded.reserve(default_vector.size());
        auto encoder = cbor::tags::make_encoder(encoded);
        auto tagged  = cbor::tags::make_tag_pair(cbor::tags::static_tag<vector_tag>{}, vector_values);
        auto result  = encoder(tagged);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(encoded);
    });

    bench.run("custom_codec_1 tagged vector<double> encode", [&] {
        byte_buffer encoded;
        encoded.reserve(custom_vector.size());
        auto encoder = cbor::tags::make_encoder<cc1::custom_codec_1>(encoded);
        auto result  = encoder(cc1::as_custom_codec_1(cbor::tags::static_tag<vector_tag>{}, vector_values));
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(encoded);
    });
}

TEST_CASE("custom_codec_1 decode comparison benchmarks") {
    auto const record        = make_record();
    auto const vector_values = make_vector_values();

    auto const default_record = encode_default(record);
    auto const custom_record  = encode_custom(record);
    auto const default_vector = encode_default_tagged<vector_tag>(vector_values);
    auto const custom_vector  = encode_custom_tagged<vector_tag>(vector_values);

    ankerl::nanobench::Bench bench;
    configure_bench(bench, "custom_codec_1 decode vs default CBOR");

    bench.run("default tagged record decode", [&] {
        tagged_record decoded{};
        auto          decoder = cbor::tags::make_decoder(default_record);
        auto          result  = decoder(decoded);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(decoded);
    });

    bench.run("custom_codec_1 tagged record decode", [&] {
        tagged_record decoded{};
        auto          decoder = cbor::tags::make_decoder<cc1::custom_codec_1>(custom_record);
        auto          result  = decoder(cc1::as_custom_codec_1(decoded));
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(decoded);
    });

    bench.run("default tagged vector<double> decode", [&] {
        std::vector<double> decoded;
        auto                decoder = cbor::tags::make_decoder(default_vector);
        auto                tagged  = cbor::tags::make_tag_pair(cbor::tags::static_tag<vector_tag>{}, decoded);
        auto                result  = decoder(tagged);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(decoded);
    });

    bench.run("custom_codec_1 tagged vector<double> decode", [&] {
        std::vector<double> decoded;
        auto                decoder = cbor::tags::make_decoder<cc1::custom_codec_1>(custom_vector);
        auto                result  = decoder(cc1::as_custom_codec_1(cbor::tags::static_tag<vector_tag>{}, decoded));
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(decoded);
    });
}

TEST_CASE("custom_codec_1 encode wire throughput benchmarks") {
    constexpr auto vector_counts = std::array<std::size_t, 3>{16U, 1024U, 65536U};

    auto const record         = make_record();
    auto const default_record = encode_default(record);
    auto const custom_record  = encode_custom(record);

    ankerl::nanobench::Bench bench;
    configure_throughput_bench(bench, "custom_codec_1 encode wire throughput");

    bench.batch(default_record.size()).run("default tagged record encode", [&] {
        byte_buffer encoded;
        encoded.reserve(default_record.size());
        auto encoder = cbor::tags::make_encoder(encoded);
        auto result  = encoder(record);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(encoded);
    });

    bench.batch(custom_record.size()).run("custom_codec_1 tagged record encode", [&] {
        byte_buffer encoded;
        encoded.reserve(custom_record.size());
        auto encoder = cbor::tags::make_encoder<cc1::custom_codec_1>(encoded);
        auto result  = encoder(cc1::as_custom_codec_1(record));
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(encoded);
    });

    for (auto count : vector_counts) {
        auto const values         = make_vector_values(count);
        auto const default_vector = encode_default_tagged<vector_tag>(values);
        auto const custom_vector  = encode_custom_tagged<vector_tag>(values);

        auto const default_name = std::string{"default tagged vector<double>["} + std::to_string(count) + "] encode";
        bench.batch(default_vector.size()).run(default_name, [&] {
            byte_buffer encoded;
            encoded.reserve(default_vector.size());
            auto encoder = cbor::tags::make_encoder(encoded);
            auto tagged  = cbor::tags::make_tag_pair(cbor::tags::static_tag<vector_tag>{}, values);
            auto result  = encoder(tagged);
            ankerl::nanobench::doNotOptimizeAway(result);
            ankerl::nanobench::doNotOptimizeAway(encoded);
        });

        auto const custom_name = std::string{"custom_codec_1 tagged vector<double>["} + std::to_string(count) + "] encode";
        bench.batch(custom_vector.size()).run(custom_name, [&] {
            byte_buffer encoded;
            encoded.reserve(custom_vector.size());
            auto encoder = cbor::tags::make_encoder<cc1::custom_codec_1>(encoded);
            auto result  = encoder(cc1::as_custom_codec_1(cbor::tags::static_tag<vector_tag>{}, values));
            ankerl::nanobench::doNotOptimizeAway(result);
            ankerl::nanobench::doNotOptimizeAway(encoded);
        });
    }
}

TEST_CASE("custom_codec_1 decode wire throughput benchmarks") {
    constexpr auto vector_counts = std::array<std::size_t, 3>{16U, 1024U, 65536U};

    auto const record         = make_record();
    auto const default_record = encode_default(record);
    auto const custom_record  = encode_custom(record);

    ankerl::nanobench::Bench bench;
    configure_throughput_bench(bench, "custom_codec_1 decode wire throughput");

    bench.batch(default_record.size()).run("default tagged record decode", [&] {
        tagged_record decoded{};
        auto          decoder = cbor::tags::make_decoder(default_record);
        auto          result  = decoder(decoded);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(decoded);
    });

    bench.batch(custom_record.size()).run("custom_codec_1 tagged record decode", [&] {
        tagged_record decoded{};
        auto          decoder = cbor::tags::make_decoder<cc1::custom_codec_1>(custom_record);
        auto          result  = decoder(cc1::as_custom_codec_1(decoded));
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(decoded);
    });

    for (auto count : vector_counts) {
        auto const values         = make_vector_values(count);
        auto const default_vector = encode_default_tagged<vector_tag>(values);
        auto const custom_vector  = encode_custom_tagged<vector_tag>(values);

        CHECK(decode_default_tagged<vector_tag, std::vector<double>>(default_vector) == values);
        CHECK(decode_custom_tagged<vector_tag, std::vector<double>>(custom_vector) == values);

        auto const default_name = std::string{"default tagged vector<double>["} + std::to_string(count) + "] decode";
        bench.batch(default_vector.size()).run(default_name, [&] {
            std::vector<double> decoded;
            auto                decoder = cbor::tags::make_decoder(default_vector);
            auto                tagged  = cbor::tags::make_tag_pair(cbor::tags::static_tag<vector_tag>{}, decoded);
            auto                result  = decoder(tagged);
            ankerl::nanobench::doNotOptimizeAway(result);
            ankerl::nanobench::doNotOptimizeAway(decoded);
        });

        auto const custom_name = std::string{"custom_codec_1 tagged vector<double>["} + std::to_string(count) + "] decode";
        bench.batch(custom_vector.size()).run(custom_name, [&] {
            std::vector<double> decoded;
            auto                decoder = cbor::tags::make_decoder<cc1::custom_codec_1>(custom_vector);
            auto                result  = decoder(cc1::as_custom_codec_1(cbor::tags::static_tag<vector_tag>{}, decoded));
            ankerl::nanobench::doNotOptimizeAway(result);
            ankerl::nanobench::doNotOptimizeAway(decoded);
        });
    }
}

int main(int argc, char **argv) { return doctest::Context(argc, argv).run(); }
