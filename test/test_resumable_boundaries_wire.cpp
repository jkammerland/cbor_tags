#include "test_util.h"

#include <array>
#include <bit>
#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

using namespace cbor::tags;

namespace {

template <typename T> void scalar_boundaries(const char *hex, T expected_value) {
    const auto                       wire = to_bytes(hex);
    const std::span<const std::byte> bytes{wire};
    for (std::size_t cut = 0; cut <= bytes.size(); ++cut) {
        CAPTURE(hex);
        CAPTURE(cut);
        T          decoded{};
        auto       operation = make_resumable_decoder()(decoded);
        const auto progress  = operation(bytes.first(cut));
        if (cut < bytes.size()) {
            CHECK(progress.state == resumable_state::need_input);
            CHECK_FALSE(operation.done());
            (void)operation(bytes.subspan(cut));
        }
        REQUIRE(operation.result());
        CHECK(decoded == expected_value);
        CHECK_EQ(*operation.result(), bytes.size());

        if (cut < bytes.size()) {
            T    incomplete_value{};
            auto incomplete = make_resumable_decoder()(incomplete_value);
            (void)incomplete(bytes.first(cut), end_of_input);
            REQUIRE_FALSE(incomplete.result());
            CHECK(incomplete.result().error() == status_code::incomplete);
        }
    }

    auto                   encoder = make_resumable_encoder()(expected_value);
    std::vector<std::byte> encoded;
    while (!encoder.done() && encoded.size() <= bytes.size()) {
        std::array<std::byte, 1> output{};
        const auto               progress = encoder(output);
        for (const auto prefix : progress.produced())
            for (const auto byte : prefix)
                encoded.push_back(byte);
    }
    REQUIRE(encoder.result());
    CHECK_EQ(to_hex(encoded), std::string{hex});
}

struct tracking_bytes {
    using value_type = std::byte;
    using size_type  = std::size_t;
    std::vector<std::byte> values;
    std::size_t            reservations{};

    auto      begin() { return values.begin(); }
    auto      end() { return values.end(); }
    auto      begin() const { return values.begin(); }
    auto      end() const { return values.end(); }
    size_type size() const { return values.size(); }
    void      push_back(std::byte value) { values.push_back(value); }
    void      reserve(size_type count) {
        ++reservations;
        values.reserve(count);
    }
};

} // namespace

TEST_SUITE("cbor_wire/resumable_boundaries") {

    TEST_CASE("integer and floating point headers resume at every byte") {
        scalar_boundaries<std::uint64_t>("00", 0U);
        scalar_boundaries<std::uint64_t>("1818", 24U);
        scalar_boundaries<std::uint64_t>("19aabb", 0xaabbU);
        scalar_boundaries<std::uint64_t>("1a01020304", 0x01020304U);
        scalar_boundaries<std::uint64_t>("1b0102030405060708", 0x0102030405060708ULL);
        scalar_boundaries<float16_t>("f93c00", float16_t{static_cast<std::uint16_t>(0x3c00)});
        scalar_boundaries<float>("fa3f800000", 1.0F);
        scalar_boundaries<double>("fb3ff0000000000000", 1.0);
    }

    TEST_CASE("declared string length does not reserve before payload availability") {
        const auto     header = to_bytes("5affffffff");
        tracking_bytes destination;
        auto           operation = make_resumable_decoder()(destination);
        const auto     partial   = operation(header);
        CHECK(partial.state == resumable_state::need_input);
        CHECK_EQ(destination.reservations, 0U);
        CHECK(destination.values.empty());
        (void)operation(end_of_input);
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::incomplete);
        CHECK_EQ(destination.reservations, 0U);
    }

    TEST_CASE("fixed and bounded destinations reject declared lengths") {
        const auto               input = to_bytes("43010203");
        std::array<std::byte, 2> fixed{};
        auto                     fixed_op = make_resumable_decoder()(fixed);
        (void)fixed_op(input);
        REQUIRE_FALSE(fixed_op.result());
        CHECK(fixed_op.result().error() == status_code::unexpected_group_size);
        std::vector<std::byte> bounded;
        auto                   bounded_op = make_resumable_decoder()(as_bounded_size<1, 2>(bounded));
        (void)bounded_op(input);
        REQUIRE_FALSE(bounded_op.result());
        CHECK(bounded_op.result().error() == status_code::size_limit_exceeded);
        CHECK(bounded.empty());
    }

    TEST_CASE("an indefinite wrapped group has the existing size error") {
        const auto                         input = to_bytes("9f0102ff");
        std::optional<std::pair<int, int>> destination;
        auto                               operation = make_resumable_decoder()(destination);
        (void)operation(input);
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::unexpected_group_size);
    }

    TEST_CASE("payload generator reports incomplete only after its last prefix is released") {
        const auto input   = to_bytes("4301");
        auto       payload = make_resumable_decoder().bstr();
        (void)payload(input, end_of_input);
        auto first = payload.next();
        REQUIRE(first.await_ready());
        REQUIRE(first.await_resume());
        std::vector<std::byte> prefix;
        for (const auto byte : payload.value())
            prefix.push_back(byte);
        CHECK_EQ(to_hex(prefix), "01");
        auto last = payload.next();
        REQUIRE(last.await_ready());
        CHECK_FALSE(last.await_resume());
        REQUIRE_FALSE(payload.result());
        CHECK(payload.result().error() == status_code::incomplete);
    }
}
