#include "resumable_ring_fixture.h"
#include "test_util.h"

#include <algorithm>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_ranges.h>
#include <cbor_tags/cbor_resumable.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <list>
#include <map>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace cbor::tags;

namespace {

// Pseudo-random bytes, so a dropped, repeated or reordered piece shows up
std::vector<std::byte> payload_bytes(std::size_t size) {
    std::vector<std::byte> bytes(size);
    std::uint32_t          state = 7;
    for (auto &byte : bytes) {
        state = state * 1664525U + 1013904223U;
        byte  = static_cast<std::byte>(state >> 24);
    }
    return bytes;
}

std::string payload_text(std::size_t size) {
    std::string text(size, ' ');
    for (std::size_t index = 0; index < size; ++index)
        text[index] = static_cast<char>('a' + index % 26);
    return text;
}

// One value of each sink kind, encoded definite or indefinite
struct message {
    std::vector<std::byte>                    blob = payload_bytes(300);
    std::string                               text = payload_text(100);
    std::vector<std::string>                  names{"ada", std::string(40, 'g'), ""};
    std::map<std::uint32_t, std::vector<int>> groups{{1, {1, 2, 3}}, {70000, {}}, {2, {-1, 1000000}}};

    std::vector<std::byte> wire() const {
        std::vector<std::byte> bytes;
        REQUIRE(make_encoder(bytes)(blob, text, names, groups));
        return bytes;
    }

    // Unsized ranges encode indefinite, strings in 64-byte chunks
    std::vector<std::byte> indefinite_wire() const {
        constexpr auto         all = std::views::filter([](const auto &) { return true; });
        std::vector<std::byte> bytes;
        REQUIRE(make_encoder(bytes)(as_bstr_range(blob | all, 64), as_tstr_range(text | all, 64), as_array_range(names | all),
                                    as_map_range(groups | all)));
        return bytes;
    }
};

// What the sinks saw, piece by piece
struct received {
    std::vector<std::byte>                    blob;
    std::size_t                               blob_pieces{};
    std::string                               text;
    std::vector<std::string>                  names;
    std::map<std::uint32_t, std::vector<int>> groups;

    auto blob_sink() {
        return as_bstr_sink([this](std::span<const std::byte> piece) {
            CHECK_FALSE(piece.empty());
            blob.insert(blob.end(), piece.begin(), piece.end());
            ++blob_pieces;
        });
    }
    auto text_sink() {
        return as_tstr_sink([this](std::string_view piece) { text += piece; });
    }
    auto names_sink() {
        return as_array_sink<std::string>([this](std::string name) { names.push_back(std::move(name)); });
    }
    auto groups_sink() {
        return as_map_sink<std::uint32_t, std::vector<int>>(
            [this](std::uint32_t key, std::vector<int> values) { CHECK(groups.emplace(key, std::move(values)).second); });
    }

    void check(const message &sent) const {
        CHECK(blob == sent.blob);
        CHECK(text == sent.text);
        CHECK(names == sent.names);
        CHECK(groups == sent.groups);
    }
};

template <typename Buffer> void check_one_shot(const message &sent, const std::vector<std::byte> &wire) {
    const Buffer input(wire.begin(), wire.end());
    received     got;
    auto         decoder = make_decoder(input);
    REQUIRE(decoder(got.blob_sink(), got.text_sink(), got.names_sink(), got.groups_sink()));
    got.check(sent);
}

// Decodes wire with the first cut bytes in one chunk and the rest in another
received resumable_at_cut(const std::vector<std::byte> &wire, std::size_t cut) {
    std::list<std::span<const std::byte>> storage;
    auto                                  source  = make_input_source(storage);
    auto                                  decoder = make_resumable_decoder(source);
    received                              got;
    auto                                  operation = decoder(got.blob_sink(), got.text_sink(), got.names_sink(), got.groups_sink());
    storage.emplace_back(std::span<const std::byte>{wire}.first(cut));
    operation.resume();
    storage.emplace_back(std::span<const std::byte>{wire}.subspan(cut));
    operation.resume();
    REQUIRE(operation.done());
    REQUIRE(operation.result());
    CHECK(*operation.result() == wire.size());
    return got;
}

// Pushes a definite byte string through a ring smaller than its payload,
// reclaiming everything committed before each refill
template <std::size_t Capacity, bool Windows> struct ring_run {
    std::vector<std::byte> seen;
    std::size_t            pieces{};
    std::size_t            borrowed{};
    std::size_t            largest{};
    std::size_t            resumes{};
    bool                   committed_what_it_saw = true;

    expected<std::size_t, status_code> run(const std::vector<std::byte> &wire, std::size_t payload_start, std::size_t feed,
                                           std::size_t cancel_at = std::numeric_limits<std::size_t>::max()) {
        resumable_test::ring_source<false, Capacity, Windows> ring;
        const auto                                            origin = ring.committed_position().offset();
        // A sink drains the ring as it goes, so a full ring never means a stall
        ring.reports_capacity = true;

        auto decoder   = make_resumable_decoder(ring);
        auto operation = decoder(as_bstr_sink([&](std::span<const std::byte> piece) {
            ++pieces;
            largest = std::max(largest, piece.size());
            if (ring.stores(piece.data()))
                ++borrowed;
            seen.insert(seen.end(), piece.begin(), piece.end());
        }));
        // Once the payload started, exactly the bytes handed out are committed
        auto check_committed = [&] {
            if (!seen.empty() && ring.committed_position().offset() != origin + payload_start + seen.size())
                committed_what_it_saw = false;
        };
        for (std::size_t supplied = 0; !operation.done();) {
            // A ring that stops draining would spin here forever
            REQUIRE(resumes <= wire.size());
            ring.reclaim(ring.committed_position().offset());
            const auto rest = std::span<const std::byte>{wire}.subspan(supplied);
            supplied += ring.append(rest.first(std::min(rest.size(), feed)));
            if (resumes == cancel_at)
                operation.cancel();
            else {
                operation.resume();
                ++resumes;
            }
            check_committed();
        }
        return operation.result();
    }
};

template <typename Result> status_code status_of(const Result &result) { return result ? status_code::success : result.error(); }

// The one-shot decode into an owning destination is the reference for both decoders
template <typename Owning, typename MakeSink> void check_fails_like(const std::vector<std::byte> &wire, MakeSink make_sink) {
    Owning     owning{};
    const auto want = status_of(make_decoder(wire)(owning));
    REQUIRE(want != status_code::success);

    auto sink = make_sink();
    CHECK(status_of(make_decoder(wire)(sink)) == want);

    std::vector<std::byte> input(wire.begin(), wire.end());
    auto                   decoder   = make_resumable_decoder(input);
    auto                   operation = decoder(make_sink());
    REQUIRE(operation.done()); // everything needed to fail is there
    CHECK(status_of(operation.result()) == want);
}

} // namespace

TEST_SUITE("roundtrip/sinks") {

    TEST_CASE("sinks decode the same values as owning destinations") {
        const message sent;
        for (const bool indefinite : {false, true}) {
            CAPTURE(indefinite);
            const auto wire = indefinite ? sent.indefinite_wire() : sent.wire();
            check_one_shot<std::vector<std::byte>>(sent, wire);
            check_one_shot<std::deque<std::byte>>(sent, wire);
            check_one_shot<std::list<std::byte>>(sent, wire);
        }
    }

    TEST_CASE("contiguous input hands a definite byte string over in place") {
        const message sent;
        const auto    wire = sent.wire();

        std::size_t pieces{};
        bool        in_place{};
        auto        decoder = make_decoder(wire);
        REQUIRE(decoder(as_bstr_sink([&](std::span<const std::byte> piece) {
            ++pieces;
            in_place = piece.data() >= wire.data() && piece.data() + piece.size() <= wire.data() + wire.size();
        })));
        CHECK(pieces == 1);
        CHECK(in_place);
    }

    TEST_CASE("non-contiguous one-shot input arrives in 256-byte copies") {
        const auto             blob = payload_bytes(1000);
        std::vector<std::byte> wire;
        REQUIRE(make_encoder(wire)(blob));
        const std::deque<std::byte> input(wire.begin(), wire.end());

        std::vector<std::size_t> sizes;
        std::vector<std::byte>   seen;
        auto                     decoder = make_decoder(input);
        REQUIRE(decoder(as_bstr_sink([&](std::span<const std::byte> piece) {
            sizes.push_back(piece.size());
            seen.insert(seen.end(), piece.begin(), piece.end());
        })));
        CHECK(sizes == std::vector<std::size_t>{256, 256, 256, 232});
        CHECK(seen == blob);
    }

    TEST_CASE("empty strings, chunks and containers never call the callback") {
        // h'', (_ ""), (_ h'', h'01'), [], {_ }
        const auto               wire = to_bytes("40"
                                                 "7fff"
                                                 "5f404101ff"
                                                 "80"
                                                 "bfff");
        std::vector<std::size_t> pieces;
        auto                     bytes = as_bstr_sink([&](std::span<const std::byte> piece) { pieces.push_back(piece.size()); });
        auto                     text  = as_tstr_sink([&](std::string_view piece) { pieces.push_back(piece.size()); });
        auto                     array = as_array_sink<int>([&](int) { pieces.push_back(0); });
        auto                     map   = as_map_sink<int, int>([&](int, int) { pieces.push_back(0); });

        auto decoder = make_decoder(wire);
        REQUIRE(decoder(bytes, text, bytes, array, map));
        CHECK(pieces == std::vector<std::size_t>{1});

        pieces.clear();
        std::vector<std::byte> input(wire.begin(), wire.end());
        auto                   resumable = make_resumable_decoder(input);
        auto                   operation = resumable(bytes, text, bytes, array, map);
        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(pieces == std::vector<std::size_t>{1});
    }

    TEST_CASE("sinks work as record fields") {
        const auto             blob = payload_bytes(100);
        std::vector<std::byte> wire;
        REQUIRE(make_encoder(wire)(wrap_as_array{std::uint32_t{7}, blob, std::string{"done"}}));

        std::vector<std::byte> seen;
        auto                   collect = [&seen](std::span<const std::byte> piece) { seen.insert(seen.end(), piece.begin(), piece.end()); };
        struct upload {
            std::uint32_t                   id{};
            decltype(as_bstr_sink(collect)) data;
            std::string                     note;
        };
        upload value{.id = 0, .data = as_bstr_sink(collect), .note = {}};
        {
            auto decoder = make_decoder(wire);
            REQUIRE(decoder(value));
        }
        CHECK(value.id == 7);
        CHECK(seen == blob);
        CHECK(value.note == "done");

        // The resumable decoder streams the same field
        seen.clear();
        value.note.clear();
        std::vector<std::byte> input;
        auto                   decoder   = make_resumable_decoder(input);
        auto                   operation = decoder(value);
        for (const auto byte : wire) {
            REQUIRE_FALSE(operation.done());
            input.push_back(byte);
            operation.resume();
        }
        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(seen == blob);
        CHECK(value.note == "done");
    }

    TEST_CASE("resumable sinks see every byte once at every split") {
        const message sent;
        for (const bool indefinite : {false, true}) {
            CAPTURE(indefinite);
            const auto wire = indefinite ? sent.indefinite_wire() : sent.wire();
            for (std::size_t cut = 0; cut <= wire.size(); ++cut) {
                CAPTURE(cut);
                resumable_at_cut(wire, cut).check(sent);
            }
        }
    }

    TEST_CASE("resumable sinks see every byte once when input arrives a byte at a time") {
        const message sent;
        for (const bool indefinite : {false, true}) {
            CAPTURE(indefinite);
            const auto             wire = indefinite ? sent.indefinite_wire() : sent.wire();
            std::vector<std::byte> input;
            auto                   decoder = make_resumable_decoder(input);
            received               got;
            auto                   operation = decoder(got.blob_sink(), got.text_sink(), got.names_sink(), got.groups_sink());
            for (const auto byte : wire) {
                REQUIRE_FALSE(operation.done());
                input.push_back(byte);
                operation.resume();
            }
            REQUIRE(operation.done());
            REQUIRE(operation.result());
            got.check(sent);
        }
    }

    TEST_CASE("a byte string bigger than the ring streams through it") {
        const auto             blob = payload_bytes(10'000);
        std::vector<std::byte> wire;
        REQUIRE(make_encoder(wire)(blob));
        const auto head = wire.size() - blob.size(); // 3 bytes, 59 27 10

        SUBCASE("windows hand out pieces of the ring itself") {
            for (const std::size_t feed : {1U, 5U, 9U}) {
                CAPTURE(feed);
                ring_run<9, true> ring;
                const auto        result = ring.run(wire, head, feed);
                REQUIRE(result);
                CHECK(*result == wire.size());
                CHECK(ring.seen == blob);
                CHECK(ring.borrowed == ring.pieces);
                CHECK(ring.largest <= std::min<std::size_t>(feed, 9));
                CHECK(ring.committed_what_it_saw);
            }
        }

        SUBCASE("without windows the bytes come in copies") {
            ring_run<16, false> ring;
            const auto          result = ring.run(wire, head, 16);
            REQUIRE(result);
            CHECK(ring.seen == blob);
            CHECK(ring.borrowed == 0);
            CHECK(ring.pieces <= ring.resumes); // one copy per refill, not one call per byte
            CHECK(ring.committed_what_it_saw);
        }

        SUBCASE("a ring as small as the string head is enough") {
            ring_run<3, true> ring;
            const auto        result = ring.run(wire, head, 3);
            REQUIRE(result);
            CHECK(ring.seen == blob);
        }
    }

    TEST_CASE("a cancelled string keeps exactly what the sink saw committed") {
        const auto             blob = payload_bytes(1'000);
        std::vector<std::byte> wire;
        REQUIRE(make_encoder(wire)(blob));

        ring_run<9, true> ring;
        const auto        result = ring.run(wire, wire.size() - blob.size(), 4, 50);
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::cancelled);
        CHECK_FALSE(ring.seen.empty());
        CHECK(ring.seen.size() < blob.size());
        CHECK(std::ranges::equal(ring.seen, std::span{blob}.first(ring.seen.size())));
        CHECK(ring.committed_what_it_saw);
    }

    TEST_CASE("arrays and maps stream through a ring one element at a time") {
        std::vector<std::string> names;
        for (int index = 0; index < 200; ++index)
            names.push_back("name-" + std::to_string(index));
        const std::map<std::uint32_t, std::vector<int>> groups{{1, {1, 2, 3}}, {2, {}}, {70000, {5, -6}}};
        std::vector<std::byte>                          wire;
        REQUIRE(make_encoder(wire)(names, groups));
        REQUIRE(wire.size() > 1'000);

        resumable_test::ring_source<false, 16, true> ring;
        std::vector<std::string>                     got_names;
        std::map<std::uint32_t, std::vector<int>>    got_groups;
        auto                                         decoder = make_resumable_decoder(ring);

        // Elements commit as they go, so a full ring never means a stall
        ring.reports_capacity = true;
        auto operation = decoder(as_array_sink<std::string>([&](std::string name) { got_names.push_back(std::move(name)); }),
                                 as_map_sink<std::uint32_t, std::vector<int>>(
                                     [&](std::uint32_t key, std::vector<int> values) { got_groups.emplace(key, std::move(values)); }));
        for (std::size_t supplied = 0; !operation.done();) {
            ring.reclaim(ring.committed_position().offset());
            const auto rest = std::span<const std::byte>{wire}.subspan(supplied);
            supplied += ring.append(rest.first(std::min<std::size_t>(rest.size(), 3)));
            operation.resume();
        }
        REQUIRE(operation.result());
        CHECK(got_names == names);
        CHECK(got_groups == groups);
    }
}

TEST_SUITE("cbor_wire/sinks") {

    TEST_CASE("a status_code callback stops the decode with its status") {
        const auto wire = to_bytes("43010203"
                                   "83010203");

        SUBCASE("one-shot") {
            std::vector<int> seen;
            auto             decoder = make_decoder(wire);
            const auto       result =
                decoder(as_bstr_sink([](std::span<const std::byte>) { return status_code::success; }), as_array_sink<int>([&](int value) {
                            seen.push_back(value);
                            return value == 2 ? status_code::size_limit_exceeded : status_code::success;
                        }));
            REQUIRE_FALSE(result);
            CHECK(result.error() == status_code::size_limit_exceeded);
            CHECK(seen == std::vector<int>{1, 2});
        }

        SUBCASE("resumable") {
            std::vector<std::byte> input(wire.begin(), wire.end());
            auto                   decoder = make_resumable_decoder(input);
            auto operation = decoder(as_bstr_sink([](std::span<const std::byte>) { return status_code::unexpected_group_size; }));
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::unexpected_group_size);
            CHECK(operation.committed_bytes() == 1); // the header, not the rejected piece
        }
    }

    TEST_CASE("a throwing callback ends the decode with status_code::error") {
        const auto wire = to_bytes("63616263");

        auto throwing = [] { return as_tstr_sink([](std::string_view) { throw std::runtime_error("rejected"); }); };

        auto decoder = make_decoder(wire);
        auto result  = decoder(throwing());
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::error);

        std::vector<std::byte> input(wire.begin(), wire.end());
        auto                   resumable_decoder = make_resumable_decoder(input);
        auto                   operation         = resumable_decoder(throwing());
        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::error);
    }

    TEST_CASE("sinks reject malformed and mismatched input like owning destinations") {
        // Wrong majors, chunks of the other string type, nested indefinite
        // chunks, a break between a key and its value, reserved length encodings
        for (const char *hex : {"01", "5f6161ff", "7f4161ff", "5f5fffff", "7f7fffff", "bf01ff", "5c", "5f5cff", "7f7cff", "9c", "bc"}) {
            CAPTURE(hex);
            const auto wire = to_bytes(hex);
            check_fails_like<std::vector<std::byte>>(wire, [] { return as_bstr_sink([](std::span<const std::byte>) {}); });
            check_fails_like<std::string>(wire, [] { return as_tstr_sink([](std::string_view) {}); });
            check_fails_like<std::vector<int>>(wire, [] { return as_array_sink<int>([](int) {}); });
            check_fails_like<std::map<int, int>>(wire, [] { return as_map_sink<int, int>([](int, int) {}); });
        }
    }

    TEST_CASE("truncated input leaves one-shot sinks incomplete and resumable sinks waiting") {
        for (const char *hex : {"430102", "5f4101", "6361", "8301", "9f01", "a101", "bf01"}) {
            CAPTURE(hex);
            const auto wire = to_bytes(hex);

            auto check = [&](auto make_sink) {
                auto decoder = make_decoder(wire);
                auto result  = decoder(make_sink());
                REQUIRE_FALSE(result);
                CHECK(result.error() == status_code::incomplete);

                std::vector<std::byte> input(wire.begin(), wire.end());
                auto                   resumable_decoder = make_resumable_decoder(input);
                auto                   operation         = resumable_decoder(make_sink());
                CHECK_FALSE(operation.done());
            };

            const auto major = to_bytes(hex).front() >> 5;
            if (major == std::byte{2})
                check([] { return as_bstr_sink([](std::span<const std::byte>) {}); });
            else if (major == std::byte{3})
                check([] { return as_tstr_sink([](std::string_view) {}); });
            else if (major == std::byte{4})
                check([] { return as_array_sink<int>([](int) {}); });
            else
                check([] { return as_map_sink<int, int>([](int, int) {}); });
        }
    }
}
