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

    TEST_CASE("final output generator prefixes retain the unused buffer suffix") {
        const int                      first = 1;
        const std::array<std::byte, 6> second{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}, std::byte{6}};
        std::vector<std::byte>         expected;
        REQUIRE(make_encoder(expected)(first, second));
        REQUIRE_EQ(expected.size(), 8U);

        std::array<std::byte, 8> output{};
        auto                     generator = make_resumable_encoder().chunks(first);
        const auto               initial   = generator(output);
        CHECK_EQ(initial.bytes, 1U);
        auto current = generator.next();
        REQUIRE(current.await_ready());
        REQUIRE(current.await_resume());
        REQUIRE_EQ(generator.remaining_output().size(), 1U);

        const auto reuse = [&] {
            auto       encoder  = make_resumable_encoder()(second);
            const auto progress = encoder(generator.remaining_output());
            REQUIRE(encoder.done());
            REQUIRE(encoder.result());
            CHECK_EQ(progress.bytes, 7U);
            std::vector<std::byte> prefix;
            for (const auto chunk : progress.produced())
                for (const auto byte : chunk)
                    prefix.push_back(byte);
            CHECK_EQ(prefix.size(), 7U);
            const std::vector<std::byte> combined{output.begin(), output.end()};
            CHECK(combined == expected);
        };

        SUBCASE("before acknowledging the final prefix") {
            reuse();
            std::vector<std::byte> published;
            for (const auto byte : generator.value())
                published.push_back(byte);
            REQUIRE_EQ(published.size(), 1U);
            CHECK_EQ(published.front(), expected.front());
            auto acknowledge = generator.next();
            REQUIRE(acknowledge.await_ready());
            CHECK_FALSE(acknowledge.await_resume());
            REQUIRE(generator.result());
            CHECK_EQ(generator.remaining_output().size(), 1U);
        }

        SUBCASE("after acknowledging the final prefix") {
            auto acknowledge = generator.next();
            REQUIRE(acknowledge.await_ready());
            CHECK_FALSE(acknowledge.await_resume());
            REQUIRE(generator.result());
            REQUIRE_EQ(generator.remaining_output().size(), 1U);
            reuse();
        }
    }

    TEST_CASE("final output generator skips an exhausted buffer") {
        std::array<std::byte, 1> first{};
        std::array<std::byte, 8> spare{};
        auto                     generator = make_resumable_encoder().chunks(1);
        (void)generator(first, spare);
        auto current = generator.next();
        REQUIRE(current.await_ready());
        REQUIRE(current.await_resume());
        REQUIRE_EQ(generator.remaining_output().size(), 1U);

        auto acknowledge = generator.next();
        REQUIRE(acknowledge.await_ready());
        CHECK_FALSE(acknowledge.await_resume());
        REQUIRE(generator.result());
        REQUIRE_EQ(generator.remaining_output().size(), 1U);
        auto       following = make_resumable_encoder()(2);
        const auto progress  = following(generator.remaining_output());
        REQUIRE(following.result());
        CHECK_EQ(progress.bytes, 1U);
        int first_value{};
        int second_value{};
        REQUIRE(make_decoder(first)(first_value));
        REQUIRE(make_decoder(spare)(second_value));
        CHECK_EQ(first_value, 1);
        CHECK_EQ(second_value, 2);
    }

    TEST_CASE("failed output generators retain a partially used buffer") {
        std::vector<int>         too_short{3};
        std::array<std::byte, 8> output{};
        auto                     generator = make_resumable_encoder().chunks(1, as_bounded_size<2, 2>(too_short));
        const auto               feed      = generator(output);
        CHECK_EQ(feed.bytes, 1U);
        auto current = generator.next();
        REQUIRE(current.await_ready());
        REQUIRE(current.await_resume());
        REQUIRE_EQ(generator.remaining_output().size(), 1U);

        auto acknowledge = generator.next();
        REQUIRE(acknowledge.await_ready());
        CHECK_FALSE(acknowledge.await_resume());
        REQUIRE_FALSE(generator.result());
        CHECK(generator.result().error() == status_code::size_limit_exceeded);
        REQUIRE_EQ(generator.remaining_output().size(), 1U);
        auto       following = make_resumable_encoder()(2);
        const auto progress  = following(generator.remaining_output());
        REQUIRE(following.result());
        CHECK_EQ(progress.bytes, 1U);
        int first{};
        int second{};
        REQUIRE(make_decoder(output)(first, second));
        CHECK_EQ(first, 1);
        CHECK_EQ(second, 2);
    }
}
