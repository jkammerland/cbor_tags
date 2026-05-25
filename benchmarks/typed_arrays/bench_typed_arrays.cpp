#define DOCTEST_CONFIG_IMPLEMENT

#include "doctest/doctest.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/rfc8746_typed_arrays.h>
#include <cstddef>
#include <nanobench.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace rfc8746 = cbor::tags::ext::rfc8746;

using byte_buffer = std::vector<std::byte>;

template <typename T> auto make_values(std::size_t count) -> std::vector<T> {
    std::vector<T> values;
    values.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        values.push_back(static_cast<T>(index + 1U) * T{1024} + static_cast<T>((index % 7U) + 1U) * T{0.125});
    }
    return values;
}

void configure_throughput_bench(ankerl::nanobench::Bench &bench, std::string_view title) {
    bench.title(std::string{title});
    bench.unit("byte");
    bench.relative(false);
    bench.performanceCounters(false);
    bench.minEpochIterations(20);
}

template <typename T> auto encode_le(std::vector<T> const &values) -> byte_buffer {
    byte_buffer encoded;
    auto        encoder = cbor::tags::make_encoder<rfc8746::typed_array_codec>(encoded);
    auto        result  = encoder(rfc8746::as_typed_array(values));
    CHECK(result);
    return encoded;
}

template <typename T> auto encode_be(std::vector<T> const &values) -> byte_buffer {
    byte_buffer encoded;
    auto        encoder = cbor::tags::make_encoder<rfc8746::typed_array_codec>(encoded);
    auto        result  = encoder(rfc8746::as_typed_array_be(values));
    CHECK(result);
    return encoded;
}

template <typename T> auto decode_le(byte_buffer const &encoded) -> std::vector<T> {
    rfc8746::typed_array<T> decoded;
    auto                    decoder = cbor::tags::make_decoder<rfc8746::typed_array_codec>(encoded);
    auto                    result  = decoder(decoded);
    CHECK(result);
    return decoded.values();
}

template <typename T> auto decode_be(byte_buffer const &encoded) -> std::vector<T> {
    rfc8746::typed_array_be<T> decoded;
    auto                       decoder = cbor::tags::make_decoder<rfc8746::typed_array_codec>(encoded);
    auto                       result  = decoder(decoded);
    CHECK(result);
    return decoded.values();
}

template <typename T> auto decode_le_view_copy(byte_buffer const &encoded) -> std::vector<T> {
    rfc8746::typed_array_view<T> decoded;
    auto                         decoder = cbor::tags::make_decoder<rfc8746::typed_array_codec>(encoded);
    auto                         result  = decoder(decoded);
    CHECK(result);
    return decoded.copy_values();
}

template <typename T> auto decode_be_view_copy(byte_buffer const &encoded) -> std::vector<T> {
    rfc8746::typed_array_view_be<T> decoded;
    auto                            decoder = cbor::tags::make_decoder<rfc8746::typed_array_codec>(encoded);
    auto                            result  = decoder(decoded);
    CHECK(result);
    return decoded.copy_values();
}

template <typename Encode> void run_encode(ankerl::nanobench::Bench &bench, std::string_view name, std::size_t size, Encode &&encode) {
    byte_buffer encoded;
    encoded.reserve(size);
    bench.batch(size).run(std::string{name}, [&] {
        encoded.clear();
        auto result = encode(encoded);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(encoded);
    });
}

template <typename T> void run_type_benchmarks(ankerl::nanobench::Bench &bench, std::string_view type_name, std::size_t count) {
    auto const values     = make_values<T>(count);
    auto const encoded_le = encode_le(values);
    auto const encoded_be = encode_be(values);

    CHECK(decode_le<T>(encoded_le) == values);
    CHECK(decode_be<T>(encoded_be) == values);
    CHECK(decode_le_view_copy<T>(encoded_le) == values);
    CHECK(decode_be_view_copy<T>(encoded_be) == values);
    CHECK(encoded_le.size() == encoded_be.size());

    auto const prefix = std::string{"rfc8746 "} + std::string{type_name} + "[" + std::to_string(count) + "] ";

    run_encode(bench, prefix + "LE encode", encoded_le.size(), [&](byte_buffer &encoded) {
        auto encoder = cbor::tags::make_encoder<rfc8746::typed_array_codec>(encoded);
        return encoder(rfc8746::as_typed_array(values));
    });

    run_encode(bench, prefix + "BE encode", encoded_be.size(), [&](byte_buffer &encoded) {
        auto encoder = cbor::tags::make_encoder<rfc8746::typed_array_codec>(encoded);
        return encoder(rfc8746::as_typed_array_be(values));
    });

    bench.batch(encoded_le.size()).run(prefix + "LE decode owning", [&] {
        rfc8746::typed_array<T> decoded;
        auto                    decoder = cbor::tags::make_decoder<rfc8746::typed_array_codec>(encoded_le);
        auto                    result  = decoder(decoded);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(decoded);
    });

    bench.batch(encoded_be.size()).run(prefix + "BE decode owning", [&] {
        rfc8746::typed_array_be<T> decoded;
        auto                       decoder = cbor::tags::make_decoder<rfc8746::typed_array_codec>(encoded_be);
        auto                       result  = decoder(decoded);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(decoded);
    });

    bench.batch(encoded_le.size()).run(prefix + "LE decode view copy", [&] {
        rfc8746::typed_array_view<T> decoded;
        auto                         decoder = cbor::tags::make_decoder<rfc8746::typed_array_codec>(encoded_le);
        auto                         result  = decoder(decoded);
        auto                         copied  = decoded.copy_values();
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(copied);
    });

    bench.batch(encoded_be.size()).run(prefix + "BE decode view copy", [&] {
        rfc8746::typed_array_view_be<T> decoded;
        auto                            decoder = cbor::tags::make_decoder<rfc8746::typed_array_codec>(encoded_be);
        auto                            result  = decoder(decoded);
        auto                            copied  = decoded.copy_values();
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(copied);
    });
}

} // namespace

TEST_CASE("rfc8746 typed array endian fixtures roundtrip") {
    auto const float_values  = make_values<float>(16U);
    auto const double_values = make_values<double>(16U);

    CHECK(decode_le<float>(encode_le(float_values)) == float_values);
    CHECK(decode_be<float>(encode_be(float_values)) == float_values);
    CHECK(decode_le<double>(encode_le(double_values)) == double_values);
    CHECK(decode_be<double>(encode_be(double_values)) == double_values);
}

TEST_CASE("rfc8746 typed array endian throughput benchmarks") {
    ankerl::nanobench::Bench bench;
    configure_throughput_bench(bench, "rfc8746 typed array LE vs BE throughput");

    for (auto count : {1024U, 65536U}) {
        run_type_benchmarks<float>(bench, "float", count);
        run_type_benchmarks<double>(bench, "double", count);
    }
}

int main(int argc, char **argv) { return doctest::Context(argc, argv).run(); }
