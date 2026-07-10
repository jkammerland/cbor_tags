#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/detail/cbor_item.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <doctest/doctest.h>
#include <nanobench.h>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace cbor::tags;
using byte_buffer = std::vector<std::byte>;

struct nested_array_value {
    nested_array_value() = default;

    std::size_t entered{};
    std::size_t completed{};
};

template <typename Self> struct nested_array_codec : cbor_decoder_mixin_base<Self> {
    using cbor_decoder_mixin_base<Self>::decode;

    [[nodiscard]] status_code decode(nested_array_value &value, major_type major, std::byte additional_info) {
        auto &decoder = static_cast<Self &>(*this);
        if (major != major_type::Array) {
            return status_code::no_match_for_array_on_buffer;
        }

        const auto size = decoder.decode_unsigned(additional_info);
        if (size == 0U) {
            return status_code::success;
        }
        if (size != 1U) {
            return status_code::unexpected_group_size;
        }

        ++value.entered;
        const auto result = decoder.decode(value);
        if (result == status_code::success) {
            ++value.completed;
        }
        return result;
    }
};

template <typename Value, template <typename> typename... Extensions>
auto decode_public(const byte_buffer &encoded, Value &value) -> expected<void, status_code> {
    if constexpr (sizeof...(Extensions) == 0U) {
        auto decoder = make_decoder(encoded);
        return decoder(value);
    } else {
        auto decoder = make_decoder<Extensions...>(encoded);
        return decoder(value);
    }
}

template <typename Value, template <typename> typename... Extensions>
auto decode_with_preflight(const byte_buffer &encoded, Value &value) -> expected<void, status_code> {
    auto        cursor = encoded.cbegin();
    status_code status = status_code::success;
    if (!detail::cbor_item_skipper<>::skip_item(cursor, encoded.cend(), status)) {
        return unexpected<status_code>(status);
    }
    if (cursor != encoded.cend()) {
        return unexpected<status_code>(status_code::error);
    }
    return decode_public<Value, Extensions...>(encoded, value);
}

auto make_definite_integer_array(std::size_t count) -> byte_buffer {
    const std::vector<std::uint64_t> values(count, 0U);
    byte_buffer                      encoded;
    auto                             encoder = make_encoder(encoded);
    auto                             result  = encoder(values);
    REQUIRE(result);
    return encoded;
}

auto make_indefinite_integer_array(std::size_t count) -> byte_buffer {
    byte_buffer encoded;
    encoded.reserve(count + 2U);
    encoded.push_back(std::byte{0x9F});
    encoded.insert(encoded.end(), count, std::byte{0x00});
    encoded.push_back(std::byte{0xFF});
    return encoded;
}

auto make_nested_array(std::size_t depth) -> byte_buffer {
    byte_buffer encoded(depth, std::byte{0x81});
    encoded.push_back(std::byte{0x80});
    return encoded;
}

void configure_bench(ankerl::nanobench::Bench &bench, std::string_view title) {
    bench.title(std::string{title});
    bench.unit("byte");
    bench.relative(true);
    bench.performanceCounters(false);
    bench.epochs(15);
    bench.minEpochTime(std::chrono::milliseconds{20});
    bench.minEpochIterations(20);
}

void benchmark_integer_array(std::string_view name, const byte_buffer &encoded, std::size_t count) {
    std::vector<std::uint64_t> direct_value;
    std::vector<std::uint64_t> preflight_value;
    direct_value.reserve(count);
    preflight_value.reserve(count);

    REQUIRE(decode_public(encoded, direct_value));
    REQUIRE(decode_with_preflight(encoded, preflight_value));
    REQUIRE(direct_value == preflight_value);
    direct_value.clear();
    preflight_value.clear();

    ankerl::nanobench::Bench bench;
    configure_bench(bench, name);

    bench.batch(encoded.size()).run("public direct typed decode", [&] {
        direct_value.clear();
        auto result = decode_public(encoded, direct_value);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(direct_value.data());
    });

    bench.batch(encoded.size()).run("emulated structural preflight + typed decode", [&] {
        preflight_value.clear();
        auto result = decode_with_preflight(encoded, preflight_value);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(preflight_value.data());
    });
}

void benchmark_nested_array(std::size_t depth) {
    const auto encoded = make_nested_array(depth);

    nested_array_value direct_value;
    nested_array_value preflight_value;
    REQUIRE(decode_public<nested_array_value, nested_array_codec>(encoded, direct_value));
    REQUIRE(decode_with_preflight<nested_array_value, nested_array_codec>(encoded, preflight_value));
    REQUIRE(direct_value.completed == depth);
    REQUIRE(preflight_value.completed == depth);

    ankerl::nanobench::Bench bench;
    configure_bench(bench, "nested one-element arrays, depth " + std::to_string(depth));

    bench.batch(encoded.size()).run("public direct typed decode", [&] {
        direct_value = {};
        auto result  = decode_public<nested_array_value, nested_array_codec>(encoded, direct_value);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(direct_value.completed);
    });

    bench.batch(encoded.size()).run("emulated structural preflight + typed decode", [&] {
        preflight_value = {};
        auto result     = decode_with_preflight<nested_array_value, nested_array_codec>(encoded, preflight_value);
        ankerl::nanobench::doNotOptimizeAway(result);
        ankerl::nanobench::doNotOptimizeAway(preflight_value.completed);
    });
}

} // namespace

TEST_CASE("decode preflight regression benchmarks") {
    benchmark_integer_array("definite uint array, 32 elements", make_definite_integer_array(32U), 32U);
    benchmark_integer_array("definite uint array, 4096 elements", make_definite_integer_array(4096U), 4096U);
    benchmark_integer_array("indefinite uint array, 4096 elements", make_indefinite_integer_array(4096U), 4096U);
    benchmark_nested_array(128U);
}
