#define DOCTEST_CONFIG_IMPLEMENT

#include "doctest/doctest.h"

#include <array>
#include <bit>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/custom_codec_1.h>
#include <cbor_tags/extensions/rfc8746_typed_arrays.h>
#include <cstddef>
#include <cstdint>
#include <nanobench.h>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

namespace cc1     = cbor::tags::ext::custom_codec_1;
namespace rfc8746 = cbor::tags::ext::rfc8746;

using byte_buffer = std::vector<std::byte>;

constexpr std::uint64_t record_tag       = 60001U;
constexpr std::uint64_t vector_tag       = 60002U;
constexpr std::uint64_t float_vector_tag = 60003U;

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

auto make_float_vector_values() -> std::vector<float> {
    return {
        1024.125F,  2048.25F,    4096.5F,      8192.75F,    16384.875F, 32768.125F,  65536.25F,   131072.5F,
        262144.75F, 524288.875F, 1048576.125F, 2097152.25F, 4194304.5F, 8388608.75F, 16777216.0F, 33554432.0F,
    };
}

auto make_float_vector_values(std::size_t count) -> std::vector<float> {
    if (count == 16U) {
        return make_float_vector_values();
    }

    std::vector<float> values;
    values.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        values.push_back(static_cast<float>(index + 1U) * 1024.0F + static_cast<float>((index % 7U) + 1U) * 0.125F);
    }
    return values;
}

void configure_bench(ankerl::nanobench::Bench &bench, std::string_view title) {
    bench.title(std::string{title});
    bench.unit("Ops");
    bench.relative(false);
    bench.performanceCounters(true);
    bench.minEpochIterations(100);
}

void configure_throughput_bench(ankerl::nanobench::Bench &bench, std::string_view title) {
    bench.title(std::string{title});
    bench.unit("byte");
    bench.relative(false);
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

template <typename Value> auto encode_typed_array(Value const &value) -> byte_buffer {
    byte_buffer encoded;
    auto        encoder = cbor::tags::make_encoder<rfc8746::typed_array_codec>(encoded);
    auto        result  = encoder(rfc8746::as_typed_array(value));
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

template <typename Value> auto decode_typed_array(byte_buffer const &encoded) -> Value {
    using element_type = typename Value::value_type;

    rfc8746::typed_array<element_type> decoded;
    auto                               decoder = cbor::tags::make_decoder<rfc8746::typed_array_codec>(encoded);
    auto                               result  = decoder(decoded);
    CHECK(result);
    return decoded.values();
}

template <typename Value> auto decode_typed_array_view_copy(byte_buffer const &encoded) -> Value {
    using element_type = typename Value::value_type;

    rfc8746::typed_array_view<element_type> decoded;
    auto                                    decoder = cbor::tags::make_decoder<rfc8746::typed_array_codec>(encoded);
    auto                                    result  = decoder(decoded);
    CHECK(result);
    return decoded.copy_values();
}

template <typename Encode>
void run_encode_reused(ankerl::nanobench::Bench &bench, std::string_view name, std::size_t size, Encode &&encode) {
    byte_buffer encoded;
    encoded.reserve(size);
    bench.run(std::string{name}, [&] {
        encoded.clear();
        auto result = encode(encoded);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(encoded);
    });
}

template <typename Encode>
void run_encode_reused_throughput(ankerl::nanobench::Bench &bench, std::string_view name, std::size_t size, Encode &&encode) {
    byte_buffer encoded;
    encoded.reserve(size);
    bench.batch(size).run(std::string{name}, [&] {
        encoded.clear();
        auto result = encode(encoded);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(encoded);
    });
}

} // namespace

TEST_CASE("custom_codec_1 comparison fixtures roundtrip") {
    auto const record              = make_record();
    auto const vector_values       = make_vector_values();
    auto const float_vector_values = make_float_vector_values();

    auto const default_record       = encode_default(record);
    auto const custom_record        = encode_custom(record);
    auto const default_vector       = encode_default_tagged<vector_tag>(vector_values);
    auto const custom_vector        = encode_custom_tagged<vector_tag>(vector_values);
    auto const default_float_vector = encode_default_tagged<float_vector_tag>(float_vector_values);
    auto const custom_float_vector  = encode_custom_tagged<float_vector_tag>(float_vector_values);
    auto const typed_vector         = encode_typed_array(vector_values);
    auto const typed_float_vector   = encode_typed_array(float_vector_values);

    CHECK(decode_default<tagged_record>(default_record) == record);
    CHECK(decode_custom<tagged_record>(custom_record) == record);
    CHECK(decode_default_tagged<vector_tag, std::vector<double>>(default_vector) == vector_values);
    CHECK(decode_custom_tagged<vector_tag, std::vector<double>>(custom_vector) == vector_values);
    CHECK(decode_default_tagged<float_vector_tag, std::vector<float>>(default_float_vector) == float_vector_values);
    CHECK(decode_custom_tagged<float_vector_tag, std::vector<float>>(custom_float_vector) == float_vector_values);
    CHECK(decode_typed_array<std::vector<double>>(typed_vector) == vector_values);
    CHECK(decode_typed_array_view_copy<std::vector<double>>(typed_vector) == vector_values);
    CHECK(decode_typed_array<std::vector<float>>(typed_float_vector) == float_vector_values);
    CHECK(decode_typed_array_view_copy<std::vector<float>>(typed_float_vector) == float_vector_values);

    CAPTURE(default_record.size());
    CAPTURE(custom_record.size());
    CAPTURE(default_vector.size());
    CAPTURE(custom_vector.size());
    CAPTURE(default_float_vector.size());
    CAPTURE(custom_float_vector.size());
    CAPTURE(typed_vector.size());
    CAPTURE(typed_float_vector.size());

    CHECK(custom_record.size() < default_record.size());
    CHECK(custom_vector.size() < default_vector.size());
    CHECK(custom_float_vector.size() < default_float_vector.size());
    CHECK(typed_vector.size() < custom_vector.size());
    CHECK(typed_float_vector.size() < custom_float_vector.size());
}

TEST_CASE("custom_codec_1 encode comparison benchmarks") {
    auto const record              = make_record();
    auto const vector_values       = make_vector_values();
    auto const float_vector_values = make_float_vector_values();

    auto const default_record       = encode_default(record);
    auto const custom_record        = encode_custom(record);
    auto const default_vector       = encode_default_tagged<vector_tag>(vector_values);
    auto const custom_vector        = encode_custom_tagged<vector_tag>(vector_values);
    auto const default_float_vector = encode_default_tagged<float_vector_tag>(float_vector_values);
    auto const custom_float_vector  = encode_custom_tagged<float_vector_tag>(float_vector_values);
    auto const typed_vector         = encode_typed_array(vector_values);
    auto const typed_float_vector   = encode_typed_array(float_vector_values);

    ankerl::nanobench::Bench bench;
    configure_bench(bench, "custom_codec_1 encode vs default CBOR");

    run_encode_reused(bench, "default tagged record encode", default_record.size(), [&](byte_buffer &encoded) {
        auto encoder = cbor::tags::make_encoder(encoded);
        return encoder(record);
    });

    run_encode_reused(bench, "custom_codec_1 tagged record encode", custom_record.size(), [&](byte_buffer &encoded) {
        auto encoder = cbor::tags::make_encoder<cc1::custom_codec_1>(encoded);
        return encoder(cc1::as_custom_codec_1(record));
    });

    run_encode_reused(bench, "default tagged vector<double> encode", default_vector.size(), [&](byte_buffer &encoded) {
        auto encoder = cbor::tags::make_encoder(encoded);
        auto tagged  = cbor::tags::make_tag_pair(cbor::tags::static_tag<vector_tag>{}, vector_values);
        return encoder(tagged);
    });

    run_encode_reused(bench, "custom_codec_1 tagged vector<double> encode", custom_vector.size(), [&](byte_buffer &encoded) {
        auto encoder = cbor::tags::make_encoder<cc1::custom_codec_1>(encoded);
        return encoder(cc1::as_custom_codec_1(cbor::tags::static_tag<vector_tag>{}, vector_values));
    });

    run_encode_reused(bench, "rfc8746 typed array vector<double> encode", typed_vector.size(), [&](byte_buffer &encoded) {
        auto encoder = cbor::tags::make_encoder<rfc8746::typed_array_codec>(encoded);
        return encoder(rfc8746::as_typed_array(vector_values));
    });

    run_encode_reused(bench, "default tagged vector<float> encode", default_float_vector.size(), [&](byte_buffer &encoded) {
        auto encoder = cbor::tags::make_encoder(encoded);
        auto tagged  = cbor::tags::make_tag_pair(cbor::tags::static_tag<float_vector_tag>{}, float_vector_values);
        return encoder(tagged);
    });

    run_encode_reused(bench, "custom_codec_1 tagged vector<float> encode", custom_float_vector.size(), [&](byte_buffer &encoded) {
        auto encoder = cbor::tags::make_encoder<cc1::custom_codec_1>(encoded);
        return encoder(cc1::as_custom_codec_1(cbor::tags::static_tag<float_vector_tag>{}, float_vector_values));
    });

    run_encode_reused(bench, "rfc8746 typed array vector<float> encode", typed_float_vector.size(), [&](byte_buffer &encoded) {
        auto encoder = cbor::tags::make_encoder<rfc8746::typed_array_codec>(encoded);
        return encoder(rfc8746::as_typed_array(float_vector_values));
    });
}

TEST_CASE("custom_codec_1 decode comparison benchmarks") {
    auto const record              = make_record();
    auto const vector_values       = make_vector_values();
    auto const float_vector_values = make_float_vector_values();

    auto const default_record       = encode_default(record);
    auto const custom_record        = encode_custom(record);
    auto const default_vector       = encode_default_tagged<vector_tag>(vector_values);
    auto const custom_vector        = encode_custom_tagged<vector_tag>(vector_values);
    auto const default_float_vector = encode_default_tagged<float_vector_tag>(float_vector_values);
    auto const custom_float_vector  = encode_custom_tagged<float_vector_tag>(float_vector_values);
    auto const typed_vector         = encode_typed_array(vector_values);
    auto const typed_float_vector   = encode_typed_array(float_vector_values);

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

    bench.run("rfc8746 typed array vector<double> decode owning", [&] {
        rfc8746::typed_array<double> decoded;
        auto                         decoder = cbor::tags::make_decoder<rfc8746::typed_array_codec>(typed_vector);
        auto                         result  = decoder(decoded);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(decoded);
    });

    bench.run("rfc8746 typed array view<double> decode borrowed view bind", [&] {
        rfc8746::typed_array_view<double> decoded;
        auto                              decoder = cbor::tags::make_decoder<rfc8746::typed_array_codec>(typed_vector);
        auto                              result  = decoder(decoded);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(decoded);
    });

    bench.run("default tagged vector<float> decode", [&] {
        std::vector<float> decoded;
        auto               decoder = cbor::tags::make_decoder(default_float_vector);
        auto               tagged  = cbor::tags::make_tag_pair(cbor::tags::static_tag<float_vector_tag>{}, decoded);
        auto               result  = decoder(tagged);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(decoded);
    });

    bench.run("custom_codec_1 tagged vector<float> decode", [&] {
        std::vector<float> decoded;
        auto               decoder = cbor::tags::make_decoder<cc1::custom_codec_1>(custom_float_vector);
        auto               result  = decoder(cc1::as_custom_codec_1(cbor::tags::static_tag<float_vector_tag>{}, decoded));
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(decoded);
    });

    bench.run("rfc8746 typed array vector<float> decode owning", [&] {
        rfc8746::typed_array<float> decoded;
        auto                        decoder = cbor::tags::make_decoder<rfc8746::typed_array_codec>(typed_float_vector);
        auto                        result  = decoder(decoded);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(decoded);
    });

    bench.run("rfc8746 typed array view<float> decode borrowed view bind", [&] {
        rfc8746::typed_array_view<float> decoded;
        auto                             decoder = cbor::tags::make_decoder<rfc8746::typed_array_codec>(typed_float_vector);
        auto                             result  = decoder(decoded);
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

    run_encode_reused_throughput(bench, "default tagged record encode", default_record.size(), [&](byte_buffer &encoded) {
        auto encoder = cbor::tags::make_encoder(encoded);
        return encoder(record);
    });

    run_encode_reused_throughput(bench, "custom_codec_1 tagged record encode", custom_record.size(), [&](byte_buffer &encoded) {
        auto encoder = cbor::tags::make_encoder<cc1::custom_codec_1>(encoded);
        return encoder(cc1::as_custom_codec_1(record));
    });

    for (auto count : vector_counts) {
        auto const values         = make_vector_values(count);
        auto const default_vector = encode_default_tagged<vector_tag>(values);
        auto const custom_vector  = encode_custom_tagged<vector_tag>(values);
        auto const zc_vector      = cc1::encode_borrowed_segments(cbor::tags::static_tag<vector_tag>{}, values);
        auto const typed_vector   = encode_typed_array(values);

        CHECK(zc_vector.total_size() == custom_vector.size());
        CHECK(zc_vector.flatten() == custom_vector);

        if constexpr (std::endian::native == std::endian::little) {
            auto const typed_zc_vector = rfc8746::encode_typed_array_segments(std::span<const double>{values.data(), values.size()});
            CHECK(typed_zc_vector.total_size() == typed_vector.size());
            CHECK(typed_zc_vector.flatten() == typed_vector);
        }

        auto const default_name = std::string{"default tagged vector<double>["} + std::to_string(count) + "] encode";
        run_encode_reused_throughput(bench, default_name, default_vector.size(), [&](byte_buffer &encoded) {
            auto encoder = cbor::tags::make_encoder(encoded);
            auto tagged  = cbor::tags::make_tag_pair(cbor::tags::static_tag<vector_tag>{}, values);
            return encoder(tagged);
        });

        auto const custom_name = std::string{"custom_codec_1 tagged vector<double>["} + std::to_string(count) + "] encode";
        run_encode_reused_throughput(bench, custom_name, custom_vector.size(), [&](byte_buffer &encoded) {
            auto encoder = cbor::tags::make_encoder<cc1::custom_codec_1>(encoded);
            return encoder(cc1::as_custom_codec_1(cbor::tags::static_tag<vector_tag>{}, values));
        });

        auto const zc_name =
            std::string{"custom_codec_1 zc vector<double>["} + std::to_string(count) + "] encode segment assembly (represented bytes)";
        bench.batch(zc_vector.total_size()).run(zc_name, [&] {
            auto segments = cc1::encode_borrowed_segments(cbor::tags::static_tag<vector_tag>{}, values);
            ankerl::nanobench::doNotOptimizeAway(segments);
        });

        auto const typed_name = std::string{"rfc8746 typed array vector<double>["} + std::to_string(count) + "] encode";
        run_encode_reused_throughput(bench, typed_name, typed_vector.size(), [&](byte_buffer &encoded) {
            auto encoder = cbor::tags::make_encoder<rfc8746::typed_array_codec>(encoded);
            return encoder(rfc8746::as_typed_array(values));
        });

        if constexpr (std::endian::native == std::endian::little) {
            auto const typed_zc_vector = rfc8746::encode_typed_array_segments(std::span<const double>{values.data(), values.size()});
            auto const typed_zc_name   = std::string{"rfc8746 typed array zc vector<double>["} + std::to_string(count) +
                                       "] encode segment assembly (represented bytes)";
            bench.batch(typed_zc_vector.total_size()).run(typed_zc_name, [&] {
                auto segments = rfc8746::encode_typed_array_segments(std::span<const double>{values.data(), values.size()});
                ankerl::nanobench::doNotOptimizeAway(segments);
            });
        }

        auto const float_values         = make_float_vector_values(count);
        auto const default_float_vector = encode_default_tagged<float_vector_tag>(float_values);
        auto const custom_float_vector  = encode_custom_tagged<float_vector_tag>(float_values);
        auto const zc_float_vector      = cc1::encode_borrowed_segments(cbor::tags::static_tag<float_vector_tag>{}, float_values);
        auto const typed_float_vector   = encode_typed_array(float_values);

        CHECK(zc_float_vector.total_size() == custom_float_vector.size());
        CHECK(zc_float_vector.flatten() == custom_float_vector);

        if constexpr (std::endian::native == std::endian::little) {
            auto const typed_zc_float_vector =
                rfc8746::encode_typed_array_segments(std::span<const float>{float_values.data(), float_values.size()});
            CHECK(typed_zc_float_vector.total_size() == typed_float_vector.size());
            CHECK(typed_zc_float_vector.flatten() == typed_float_vector);
        }

        auto const default_float_name = std::string{"default tagged vector<float>["} + std::to_string(count) + "] encode";
        run_encode_reused_throughput(bench, default_float_name, default_float_vector.size(), [&](byte_buffer &encoded) {
            auto encoder = cbor::tags::make_encoder(encoded);
            auto tagged  = cbor::tags::make_tag_pair(cbor::tags::static_tag<float_vector_tag>{}, float_values);
            return encoder(tagged);
        });

        auto const custom_float_name = std::string{"custom_codec_1 tagged vector<float>["} + std::to_string(count) + "] encode";
        run_encode_reused_throughput(bench, custom_float_name, custom_float_vector.size(), [&](byte_buffer &encoded) {
            auto encoder = cbor::tags::make_encoder<cc1::custom_codec_1>(encoded);
            return encoder(cc1::as_custom_codec_1(cbor::tags::static_tag<float_vector_tag>{}, float_values));
        });

        auto const zc_float_name =
            std::string{"custom_codec_1 zc vector<float>["} + std::to_string(count) + "] encode segment assembly (represented bytes)";
        bench.batch(zc_float_vector.total_size()).run(zc_float_name, [&] {
            auto segments = cc1::encode_borrowed_segments(cbor::tags::static_tag<float_vector_tag>{}, float_values);
            ankerl::nanobench::doNotOptimizeAway(segments);
        });

        auto const typed_float_name = std::string{"rfc8746 typed array vector<float>["} + std::to_string(count) + "] encode";
        run_encode_reused_throughput(bench, typed_float_name, typed_float_vector.size(), [&](byte_buffer &encoded) {
            auto encoder = cbor::tags::make_encoder<rfc8746::typed_array_codec>(encoded);
            return encoder(rfc8746::as_typed_array(float_values));
        });

        if constexpr (std::endian::native == std::endian::little) {
            auto const typed_zc_float_vector =
                rfc8746::encode_typed_array_segments(std::span<const float>{float_values.data(), float_values.size()});
            auto const typed_zc_float_name = std::string{"rfc8746 typed array zc vector<float>["} + std::to_string(count) +
                                             "] encode segment assembly (represented bytes)";
            bench.batch(typed_zc_float_vector.total_size()).run(typed_zc_float_name, [&] {
                auto segments = rfc8746::encode_typed_array_segments(std::span<const float>{float_values.data(), float_values.size()});
                ankerl::nanobench::doNotOptimizeAway(segments);
            });
        }
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
        auto const typed_vector   = encode_typed_array(values);

        CHECK(decode_default_tagged<vector_tag, std::vector<double>>(default_vector) == values);
        CHECK(decode_custom_tagged<vector_tag, std::vector<double>>(custom_vector) == values);
        CHECK(decode_typed_array<std::vector<double>>(typed_vector) == values);
        CHECK(decode_typed_array_view_copy<std::vector<double>>(typed_vector) == values);

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

        auto const typed_name = std::string{"rfc8746 typed array vector<double>["} + std::to_string(count) + "] decode owning";
        bench.batch(typed_vector.size()).run(typed_name, [&] {
            rfc8746::typed_array<double> decoded;
            auto                         decoder = cbor::tags::make_decoder<rfc8746::typed_array_codec>(typed_vector);
            auto                         result  = decoder(decoded);
            ankerl::nanobench::doNotOptimizeAway(result);
            ankerl::nanobench::doNotOptimizeAway(decoded);
        });

        auto const typed_view_name =
            std::string{"rfc8746 typed array view<double>["} + std::to_string(count) + "] decode borrowed view bind (represented bytes)";
        bench.batch(typed_vector.size()).run(typed_view_name, [&] {
            rfc8746::typed_array_view<double> decoded;
            auto                              decoder = cbor::tags::make_decoder<rfc8746::typed_array_codec>(typed_vector);
            auto                              result  = decoder(decoded);
            ankerl::nanobench::doNotOptimizeAway(result);
            ankerl::nanobench::doNotOptimizeAway(decoded);
        });

        auto const float_values         = make_float_vector_values(count);
        auto const default_float_vector = encode_default_tagged<float_vector_tag>(float_values);
        auto const custom_float_vector  = encode_custom_tagged<float_vector_tag>(float_values);
        auto const typed_float_vector   = encode_typed_array(float_values);

        CHECK(decode_default_tagged<float_vector_tag, std::vector<float>>(default_float_vector) == float_values);
        CHECK(decode_custom_tagged<float_vector_tag, std::vector<float>>(custom_float_vector) == float_values);
        CHECK(decode_typed_array<std::vector<float>>(typed_float_vector) == float_values);
        CHECK(decode_typed_array_view_copy<std::vector<float>>(typed_float_vector) == float_values);

        auto const default_float_name = std::string{"default tagged vector<float>["} + std::to_string(count) + "] decode";
        bench.batch(default_float_vector.size()).run(default_float_name, [&] {
            std::vector<float> decoded;
            auto               decoder = cbor::tags::make_decoder(default_float_vector);
            auto               tagged  = cbor::tags::make_tag_pair(cbor::tags::static_tag<float_vector_tag>{}, decoded);
            auto               result  = decoder(tagged);
            ankerl::nanobench::doNotOptimizeAway(result);
            ankerl::nanobench::doNotOptimizeAway(decoded);
        });

        auto const custom_float_name = std::string{"custom_codec_1 tagged vector<float>["} + std::to_string(count) + "] decode";
        bench.batch(custom_float_vector.size()).run(custom_float_name, [&] {
            std::vector<float> decoded;
            auto               decoder = cbor::tags::make_decoder<cc1::custom_codec_1>(custom_float_vector);
            auto               result  = decoder(cc1::as_custom_codec_1(cbor::tags::static_tag<float_vector_tag>{}, decoded));
            ankerl::nanobench::doNotOptimizeAway(result);
            ankerl::nanobench::doNotOptimizeAway(decoded);
        });

        auto const typed_float_name = std::string{"rfc8746 typed array vector<float>["} + std::to_string(count) + "] decode owning";
        bench.batch(typed_float_vector.size()).run(typed_float_name, [&] {
            rfc8746::typed_array<float> decoded;
            auto                        decoder = cbor::tags::make_decoder<rfc8746::typed_array_codec>(typed_float_vector);
            auto                        result  = decoder(decoded);
            ankerl::nanobench::doNotOptimizeAway(result);
            ankerl::nanobench::doNotOptimizeAway(decoded);
        });

        auto const typed_float_view_name =
            std::string{"rfc8746 typed array view<float>["} + std::to_string(count) + "] decode borrowed view bind (represented bytes)";
        bench.batch(typed_float_vector.size()).run(typed_float_view_name, [&] {
            rfc8746::typed_array_view<float> decoded;
            auto                             decoder = cbor::tags::make_decoder<rfc8746::typed_array_codec>(typed_float_vector);
            auto                             result  = decoder(decoded);
            ankerl::nanobench::doNotOptimizeAway(result);
            ankerl::nanobench::doNotOptimizeAway(decoded);
        });
    }
}

int main(int argc, char **argv) { return doctest::Context(argc, argv).run(); }
