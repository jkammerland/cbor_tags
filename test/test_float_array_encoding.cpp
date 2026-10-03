#include <algorithm>
#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cstddef>
#include <doctest/doctest.h>
#include <span>
#include <vector>

using namespace cbor::tags;

namespace {

template <typename Storage> void check_disjoint_shared_float_storage(Storage &storage) {
    using Float = typename decltype(storage.source)::value_type;
    for (std::size_t index = 0; index < storage.source.size(); ++index) {
        storage.source[index] = static_cast<Float>(static_cast<int>(index) - 16) / Float{8};
    }
    const auto     original_source = storage.source;
    constexpr auto guard           = std::byte{0xa5};
    storage.before.fill(guard);
    storage.between.fill(guard);
    storage.after.fill(guard);
    storage.output.fill(guard);

    auto output = std::span<std::byte>{storage.output};
    auto enc    = make_encoder(output);
    REQUIRE(enc(storage.source));
    const auto written = enc.appender_.head_;
    REQUIRE(written > 0);
    REQUIRE(written < output.size());

    const auto               encoded = std::span<const std::byte>{output}.first(written);
    decltype(storage.source) decoded{};
    auto                     dec = make_decoder(encoded);
    REQUIRE(dec(decoded));
    CHECK(decoded == original_source);
    CHECK(dec.tell() == encoded.end());
    CHECK(storage.source == original_source);

    const auto is_guard = [](std::byte value) { return value == guard; };
    CHECK(std::ranges::all_of(storage.before, is_guard));
    CHECK(std::ranges::all_of(storage.between, is_guard));
    CHECK(std::ranges::all_of(storage.after, is_guard));
    CHECK(std::ranges::all_of(output.subspan(written), is_guard));
}

} // namespace

TEST_CASE_TEMPLATE("ordinary floating arrays roundtrip with reserved output", Float, float, double) {
    for (const std::size_t count : {0U, 1U, 32U, 4096U}) {
        CAPTURE(count);
        std::vector<Float> source;
        source.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            source.push_back(static_cast<Float>(static_cast<int>(index % 257) - 128) / Float{8});
        }
        std::vector<std::byte> output;
        output.reserve(count * (sizeof(Float) + 1) + 9);
        REQUIRE(make_encoder(output)(source));
        std::vector<Float> decoded;
        REQUIRE(make_decoder(output)(decoded));
        CHECK(decoded == source);
    }
}

TEST_CASE_TEMPLATE("ordinary floating arrays roundtrip with exact fixed output", Float, float, double) {
    const std::array<Float, 3>                                     source{Float{1}, Float{-2.5}, Float{0}};
    std::array<std::byte, 1 + source.size() * (sizeof(Float) + 1)> output{};
    REQUIRE(make_encoder(output)(source));
    std::array<Float, 3> decoded{};
    REQUIRE(make_decoder(output)(decoded));
    CHECK(decoded == source);
}

TEST_CASE_TEMPLATE("ordinary floating arrays keep disjoint shared storage with source before output", Float, float, double) {
    struct backing {
        std::array<std::byte, 16>  before{};
        std::array<Float, 32>      source{};
        std::array<std::byte, 16>  between{};
        std::array<std::byte, 512> output{};
        std::array<std::byte, 16>  after{};
    };
    backing storage;
    check_disjoint_shared_float_storage(storage);
}

TEST_CASE_TEMPLATE("ordinary floating arrays keep disjoint shared storage with output before source", Float, float, double) {
    struct backing {
        std::array<std::byte, 16>  before{};
        std::array<std::byte, 512> output{};
        std::array<std::byte, 16>  between{};
        std::array<Float, 32>      source{};
        std::array<std::byte, 16>  after{};
    };
    backing storage;
    check_disjoint_shared_float_storage(storage);
}
