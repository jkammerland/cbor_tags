#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cstddef>
#include <doctest/doctest.h>
#include <new>
#include <stdexcept>
#include <vector>

using namespace cbor::tags;

namespace {

// Throw only after consuming the requested item, so the test also checks that
// exception translation preserves the ordinary terminal-progress contract.
struct throwing_record {
    int value{};
    void (*raise)();

    template <typename Decoder> expected<void, status_code> decode(Decoder &decoder) {
        const auto result = decoder(value);
        if (!result)
            return unexpected<status_code>{result.error()};
        raise();
        return {};
    }
};

} // namespace

TEST_CASE("decoder exception boundary preserves consumed input and specific statuses") {
    std::vector<std::byte> wire;
    REQUIRE(make_encoder(wire)(42));

    throwing_record value{0, [] { throw 42; }};
    auto            expected_status = status_code::error;
    SUBCASE("nonstandard exception") {}
    SUBCASE("standard exception") {
        value.raise = [] { throw std::runtime_error("custom decoder"); };
    }
    SUBCASE("allocation failure") {
        value.raise     = [] { throw std::bad_alloc{}; };
        expected_status = status_code::out_of_memory;
    }
    SUBCASE("destination length failure") {
        value.raise     = [] { throw std::length_error("custom decoder"); };
        expected_status = status_code::out_of_memory;
    }

    auto       decoder = make_decoder(wire);
    const auto result  = decoder(value);
    REQUIRE_FALSE(result);
    CHECK(result.error() == expected_status);
    CHECK(value.value == 42);
    CHECK(decoder.tell() == wire.cend());
}
