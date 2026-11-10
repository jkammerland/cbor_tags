#include "resumable_packet_fixture.h"

#include <algorithm>
#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ct = cbor::tags;

namespace {

struct packet_message {
    std::vector<std::uint64_t> readings;
    std::string                station;
    std::vector<std::byte>     payload;

    friend bool operator==(const packet_message &, const packet_message &) = default;
};

packet_message expected_message() {
    packet_message value{{0, 1ULL << 40, 24, (1ULL << 40) + 1, 65536, (1ULL << 40) + 2},
                         std::string(157, 's'),
                         std::vector<std::byte>(197)};
    for (std::size_t index = 0; index != value.payload.size(); ++index)
        value.payload[index] = static_cast<std::byte>(index);
    return value;
}

template <typename Position> void copy_bytes(std::vector<std::byte> &destination, Position first, Position last) {
    destination.insert(destination.end(), first, last);
}

} // namespace

TEST_SUITE("roundtrip/resumable_packets") {

    TEST_CASE("input span descriptors retain independently owned packets across append and empty chunks") {
        const auto             expected = expected_message();
        std::vector<std::byte> wire;
        REQUIRE(ct::make_encoder(wire)(expected));

        std::vector<std::unique_ptr<std::vector<std::byte>>> owners;
        std::deque<std::span<const std::byte>>               input;
        auto                                                 decoder = ct::make_resumable_decoder(input);
        packet_message                                       actual;
        auto                                                 operation = decoder(actual);
        REQUIRE_FALSE(operation.done());

        std::size_t supplied{};
        while (supplied != wire.size()) {
            // Empty descriptors and independently allocated owners force descriptor growth.
            input.emplace_back();
            const auto count = std::min(std::size_t{1} + owners.size() % 7, wire.size() - supplied);
            owners.push_back(std::make_unique<std::vector<std::byte>>(wire.begin() + supplied, wire.begin() + supplied + count));
            input.emplace_back(*owners.back());
            supplied += count;
            const auto state = operation.resume();
            CHECK(state == (supplied == wire.size() ? ct::resume_state::finished : ct::resume_state::need_input));

            if (!operation.done()) {
                const auto committed = operation.committed_bytes();
                CHECK(operation.resume() == ct::resume_state::need_input);
                CHECK(operation.committed_bytes() == committed);
            }
        }

        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(*operation.result() == wire.size());
        CHECK(actual == expected);
        CHECK(owners.size() > 32);
    }

    TEST_CASE("output span descriptors grow the final packet and preserve poisoned spare capacity") {
        using packet = std::array<std::byte, 11>;
        std::vector<std::unique_ptr<packet>> owners;
        std::deque<std::span<std::byte>>     output;
        auto                                 encoder   = ct::make_resumable_encoder(output);
        const auto                           expected  = expected_message();
        auto                                 operation = encoder(expected);
        REQUIRE_FALSE(operation.done());

        std::size_t before_last{};
        while (!operation.done()) {
            before_last = operation.committed_bytes();
            owners.push_back(std::make_unique_for_overwrite<packet>());
            owners.back()->fill(std::byte{0xa5});
            output.emplace_back();
            output.emplace_back(std::span{*owners.back()}.first(2));
            operation.resume();

            // Only the current final span grows; earlier descriptors remain unchanged.
            if (!operation.done()) {
                output.back() = std::span{*owners.back()};
                operation.resume();
            }
            REQUIRE(operation.committed_bytes() > before_last);
        }

        REQUIRE(operation.result());
        std::vector<std::byte> produced;
        auto                   remaining = operation.committed_bytes();
        for (const auto chunk : output) {
            const auto count = std::min(remaining, chunk.size());
            produced.insert(produced.end(), chunk.begin(), chunk.begin() + count);
            remaining -= count;
        }
        REQUIRE(remaining == 0);
        REQUIRE(owners.size() > 16);
        const auto preceding_capacity = (owners.size() - 1) * owners.front()->size();
        REQUIRE(operation.committed_bytes() >= preceding_capacity);
        const auto used_last = operation.committed_bytes() - preceding_capacity;
        REQUIRE(used_last <= owners.back()->size());
        for (std::size_t index = used_last; index != owners.back()->size(); ++index)
            CHECK((*owners.back())[index] == std::byte{0xa5});

        packet_message actual;
        REQUIRE(ct::make_decoder(produced)(actual));
        CHECK(actual == expected);
    }

    TEST_CASE("bounded input packet slots retain incomplete payload while committed slots are reused") {
        const auto             expected = expected_message();
        std::vector<std::byte> wire;
        REQUIRE(ct::make_encoder(wire)(expected));
        resumable_test::packet_pool_source<false, 11, 3> input;
        auto                                             decoder = ct::make_resumable_decoder(input);
        packet_message                                   actual;
        auto                                             operation = decoder(actual);

        std::size_t supplied{};
        bool        retained_partial{};
        while (!operation.done()) {
            const auto old_end     = input.end().offset();
            const auto uncommitted = input.remaining_size(input.committed_position());
            input.reclaim_ready();
            CHECK(input.end().offset() == old_end);
            CHECK(input.remaining_size(input.committed_position()) == uncommitted);
            CHECK(input.begin().offset() <= input.committed_position().offset());
            retained_partial = retained_partial || uncommitted != 0;

            const auto pending  = std::span<const std::byte>{wire}.subspan(supplied);
            const auto admitted = input.append(pending.first(std::min(pending.size(), std::size_t{5})));
            REQUIRE(admitted > 0);
            supplied += admitted;
            operation.resume();
        }

        REQUIRE(operation.result());
        CHECK(*operation.result() == wire.size());
        CHECK(supplied == wire.size());
        CHECK(retained_partial);
        CHECK(input.retired_packets() > 3);
        CHECK(actual == expected);
    }

    TEST_CASE("bounded output packet slots wait for partial sends before reuse") {
        resumable_test::packet_pool_source<true, 11, 3> output;
        auto                                            encoder   = ct::make_resumable_encoder(output);
        const auto                                      expected  = expected_message();
        auto                                            operation = encoder(expected);
        REQUIRE_FALSE(operation.done());
        const auto retained = output.begin().offset();
        const auto held     = output.publish();
        REQUIRE(held);
        REQUIRE(held->last.offset() - held->first.offset() > 2);

        std::vector<std::byte> produced;
        auto                   sent_through = held->first;
        ++sent_through;
        ++sent_through;
        copy_bytes(produced, held->first, sent_through);
        const std::vector<std::byte> pending_send(sent_through, held->last);

        // A partial send keeps the original packet storage borrowed and unreclaimed.
        output.reclaim_ready();
        CHECK(output.begin().offset() == retained);
        CHECK(operation.resume() == ct::resume_state::need_output);
        CHECK(std::vector<std::byte>(sent_through, held->last) == pending_send);
        copy_bytes(produced, sent_through, held->last);
        output.complete_send(*held);
        output.reclaim_ready();
        CHECK(output.begin().offset() > retained);

        for (;;) {
            while (const auto packet = output.publish()) {
                copy_bytes(produced, packet->first, packet->last);
                output.complete_send(*packet);
            }
            output.reclaim_ready();
            if (operation.done())
                break;
            const auto committed = operation.committed_bytes();
            operation.resume();
            REQUIRE(operation.committed_bytes() > committed);
        }

        REQUIRE(operation.result());
        CHECK(produced.size() == *operation.result());
        CHECK(output.retired_packets() > 3);
        packet_message actual;
        REQUIRE(ct::make_decoder(produced)(actual));
        CHECK(actual == expected);
    }
}
