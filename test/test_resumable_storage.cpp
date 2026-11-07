#include "resumable_ring_fixture.h"

#include <algorithm>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cbor_tags/cbor_resumable_source.h>
#include <cbor_tags/codec/typed_array.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <doctest/doctest.h>
#include <limits>
#include <list>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace cbor::tags;

namespace {
struct ordinary_item {
    std::string                      value;
    template <typename Encoder> auto encode(Encoder &encoder) const { return encoder(value); }
};

template <typename Storage>
concept ReservableStorage = requires(Storage &storage) { storage.reserve(std::size_t{}); };

// Force vector relocation to move nodes on libraries whose list move can throw.
struct movable_node_chunk {
    std::list<std::byte> bytes;

    movable_node_chunk() = default;
    explicit movable_node_chunk(std::size_t size) : bytes(size) {}
    template <typename Iterator> movable_node_chunk(Iterator first, Iterator last) : bytes(first, last) {}

    movable_node_chunk(const movable_node_chunk &)            = delete;
    movable_node_chunk &operator=(const movable_node_chunk &) = delete;
    movable_node_chunk(movable_node_chunk &&)                 = default;
    movable_node_chunk &operator=(movable_node_chunk &&)      = default;

    auto        begin() { return bytes.begin(); }
    auto        end() { return bytes.end(); }
    std::size_t size() const { return bytes.size(); }
    void        push_back(std::byte value) { bytes.push_back(value); }
};

struct counted_customization {
    int *calls;

    template <typename Encoder> auto encode(Encoder &encoder) const {
        ++*calls;
        return encoder(65536);
    }
};

using ring_result = expected<std::size_t, status_code>;

// Admits up to feed bytes per resume after reclaiming what's committed. Every
// resume of a run that can finish admits a byte, so running out of resumes
// means it stalled; that returns nullopt.
template <std::size_t Capacity, bool Windows, template <typename> typename... Codecs, typename T>
std::optional<ring_result> decode_through_ring(std::span<const std::byte> wire, std::size_t feed, T &value, bool reports_capacity) {
    resumable_test::ring_source<false, Capacity, Windows> source;
    source.reports_capacity = reports_capacity;
    auto operation          = make_resumable_decoder<Codecs...>(source)(value);
    for (std::size_t resumes = 0, supplied = 0; !operation.done(); ++resumes) {
        if (resumes > wire.size() + 1) {
            operation.cancel();
            return std::nullopt;
        }
        source.reclaim(source.committed_position().offset());
        supplied += source.append(wire.subspan(supplied, std::min(feed, wire.size() - supplied)));
        operation.resume();
    }
    return operation.result();
}

// Publishes and reclaims everything committed before each resume. A unit that
// fits the ring finishes within two resumes, so the budget is two per byte.
template <std::size_t Capacity, bool Windows, template <typename> typename... Codecs, typename T>
std::optional<ring_result> encode_through_ring(const T &value, std::size_t wire_size, std::vector<std::byte> &published,
                                               bool reports_capacity) {
    resumable_test::ring_source<true, Capacity, Windows> sink;
    sink.reports_capacity = reports_capacity;
    auto operation        = make_resumable_encoder<Codecs...>(sink)(value);
    for (std::size_t resumes = 0;; ++resumes) {
        const auto committed = sink.committed_position().offset();
        for (auto at = sink.begin(); at.offset() != committed; ++at)
            published.push_back(*at);
        sink.reclaim(committed);
        if (operation.done())
            return operation.result();
        if (resumes > 2 * wire_size + 1) {
            operation.cancel();
            return std::nullopt;
        }
        operation.resume();
    }
}

// Typed arrays have no ==, so compare what they hold
template <typename T> bool same_value(const T &left, const T &right) {
    if constexpr (requires { left.values(); })
        return std::ranges::equal(left.values(), right.values());
    else
        return left == right;
}

// Reports must end exactly the runs that stall without them, and change nothing else
template <std::size_t Capacity, bool Windows, template <typename> typename... Codecs, typename T>
void check_capacity_reports(const T &value, std::size_t largest_unit) {
    std::vector<std::byte> wire;
    REQUIRE(make_encoder<Codecs...>(wire)(value));
    CAPTURE(Capacity);
    CAPTURE(Windows);
    CAPTURE(wire.size());

    // Runs finish exactly when the ring holds the value's largest whole unit
    const bool fits = Capacity >= largest_unit;

    // A byte at a time, and as much as fits so units also arrive whole across the wrap
    for (const auto feed : {std::size_t{1}, wire.size()}) {
        CAPTURE(feed);
        T          waited{};
        T          reported{};
        const auto without = decode_through_ring<Capacity, Windows, Codecs...>(wire, feed, waited, false);
        const auto with    = decode_through_ring<Capacity, Windows, Codecs...>(wire, feed, reported, true);
        REQUIRE(with.has_value());
        CHECK(without.has_value() == fits);
        if (!without) {
            REQUIRE_FALSE(*with);
            CHECK(with->error() == status_code::capacity_exceeded);
        } else {
            REQUIRE(*without);
            CHECK(same_value(waited, value));
            CHECK(*with == *without);
            CHECK(same_value(reported, waited));
        }
    }

    std::vector<std::byte> waited_bytes;
    std::vector<std::byte> reported_bytes;
    const auto             encode_without = encode_through_ring<Capacity, Windows, Codecs...>(value, wire.size(), waited_bytes, false);
    const auto             encode_with    = encode_through_ring<Capacity, Windows, Codecs...>(value, wire.size(), reported_bytes, true);
    REQUIRE(encode_with.has_value());
    CHECK(encode_without.has_value() == fits);
    if (!encode_without) {
        REQUIRE_FALSE(*encode_with);
        CHECK(encode_with->error() == status_code::capacity_exceeded);
    } else {
        REQUIRE(*encode_without);
        CHECK(waited_bytes == wire);
        CHECK(*encode_with == *encode_without);
        CHECK(reported_bytes == wire);
    }
    // A failed item never reaches the published bytes
    CHECK(reported_bytes.size() <= wire.size());
    CHECK(std::ranges::equal(reported_bytes, std::span{wire}.first(reported_bytes.size())));
}

// Rings of 1 to 12 bytes cover heads of every size, fitting or not
template <template <typename> typename... Codecs, typename T, std::size_t... Index>
void sweep_capacity_reports(const T &value, std::size_t largest_unit, std::index_sequence<Index...>) {
    (check_capacity_reports<Index + 1, false, Codecs...>(value, largest_unit), ...);
    (check_capacity_reports<Index + 1, true, Codecs...>(value, largest_unit), ...);
}
template <template <typename> typename... Codecs, typename T> void sweep_capacity_reports(const T &value, std::size_t largest_unit) {
    sweep_capacity_reports<Codecs...>(value, largest_unit, std::make_index_sequence<12>{});
}
} // namespace

namespace cbor::tags {
template <> struct resumable_retry_traits<ordinary_item> {
    static constexpr bool encode = true;
    static constexpr bool decode = false;
};
template <> struct resumable_retry_traits<counted_customization> {
    static constexpr bool encode = true;
    static constexpr bool decode = false;
};
} // namespace cbor::tags

TEST_SUITE("roundtrip/resumable_storage") {
    TEST_CASE_TEMPLATE("owning mixed byte chunks preserve input retries through tail growth and append", Storage,
                       std::vector<std::deque<std::byte>>, std::list<std::deque<std::byte>>, std::vector<std::list<std::byte>>,
                       std::deque<std::list<std::byte>>, std::vector<movable_node_chunk>) {
        const auto             expected = std::numeric_limits<std::uint64_t>::max();
        std::vector<std::byte> wire;
        REQUIRE(make_encoder(wire)(expected));
        REQUIRE(wire.size() > 4);

        for (const std::size_t cut : {std::size_t{0}, std::size_t{3}}) {
            CAPTURE(cut);
            Storage chunks;

            // A vector may copy lists during relocation, invalidating live node iterators.
            if constexpr (std::same_as<Storage, std::vector<std::list<std::byte>>>)
                chunks.reserve(3);

            chunks.emplace_back(wire.begin(), wire.begin() + static_cast<std::ptrdiff_t>(cut));
            auto          decoder = make_resumable_decoder(chunks);
            std::uint64_t actual{};
            auto          operation = decoder(actual);
            REQUIRE_FALSE(operation.done());
            CHECK(operation.committed_bytes() == 0);

            // Repeated retries must preserve the same incomplete scalar and
            // its checkpoint while owning descriptors can relocate.
            if constexpr (ReservableStorage<Storage> && !std::same_as<Storage, std::vector<std::list<std::byte>>>)
                chunks.reserve(chunks.capacity() + 2);
            for (int retry = 0; retry != 3; ++retry) {
                CHECK(operation.resume() == resume_state::need_input);
                CHECK(actual == 0);
                CHECK(operation.committed_bytes() == 0);
            }

            chunks.back().push_back(wire[cut]);
            chunks.emplace_back();
            chunks.emplace_back(wire.begin() + static_cast<std::ptrdiff_t>(cut + 1), wire.end());
            REQUIRE(operation.resume() == resume_state::finished);
            REQUIRE(operation.result());
            CHECK(actual == expected);
            CHECK(operation.committed_bytes() == wire.size());
        }
    }

    TEST_CASE_TEMPLATE("owning mixed byte chunks preserve output retries through tail growth and append", Storage,
                       std::vector<std::deque<std::byte>>, std::list<std::deque<std::byte>>, std::vector<std::list<std::byte>>,
                       std::deque<std::list<std::byte>>, std::vector<movable_node_chunk>) {
        const auto             expected = std::numeric_limits<std::uint64_t>::max();
        std::vector<std::byte> encoded;
        REQUIRE(make_encoder(encoded)(expected));
        REQUIRE(encoded.size() > 4);

        for (const std::size_t capacity : {std::size_t{0}, std::size_t{3}}) {
            CAPTURE(capacity);
            Storage chunks;

            // A vector may copy lists during relocation, invalidating live node iterators.
            if constexpr (std::same_as<Storage, std::vector<std::list<std::byte>>>)
                chunks.reserve(3);

            chunks.emplace_back(capacity);
            auto source    = make_output_source(chunks);
            auto encoder   = make_resumable_encoder(source);
            auto operation = encoder(expected);
            REQUIRE_FALSE(operation.done());
            CHECK(operation.committed_bytes() == 0);

            if constexpr (ReservableStorage<Storage> && !std::same_as<Storage, std::vector<std::list<std::byte>>>)
                chunks.reserve(chunks.capacity() + 2);
            for (int retry = 0; retry != 3; ++retry) {
                CHECK(operation.resume() == resume_state::need_output);
                CHECK(operation.committed_bytes() == 0);
                CHECK(source.committed_position().offset() == 0);
            }

            chunks.back().push_back(std::byte{});
            chunks.emplace_back();
            chunks.emplace_back(encoded.size() - capacity - 1);
            REQUIRE(operation.resume() == resume_state::finished);
            REQUIRE(operation.result());
            CHECK(operation.committed_bytes() == encoded.size());
            CHECK(source.committed_position().offset() == encoded.size());
            std::uint64_t actual{};
            REQUIRE(make_decoder(source)(actual));
            CHECK(actual == expected);
        }
    }

    TEST_CASE("pending scalar retries rebind after reserve without input growth") {
        const std::uint64_t    expected = std::uint64_t{1} << 40;
        std::vector<std::byte> wire;
        REQUIRE(make_encoder(wire)(expected));
        REQUIRE(wire.size() > 3);
        std::vector<std::byte> input(wire.begin(), wire.begin() + 3);

        const auto check = [&](auto &storage) {
            auto          decoder = make_resumable_decoder(storage);
            std::uint64_t actual{};
            auto          operation = decoder(actual);
            REQUIRE_FALSE(operation.done());
            CHECK(operation.committed_bytes() == 0);

            // The retry must reread the same incomplete representation at its new address.
            const auto *previous_data = input.data();
            input.reserve(input.capacity() + wire.size());
            REQUIRE(input.data() != previous_data);
            REQUIRE(operation.resume() == resume_state::need_input);
            CHECK(operation.committed_bytes() == 0);
            CHECK(actual == 0);

            input.insert(input.end(), wire.begin() + 3, wire.end());
            REQUIRE(operation.resume() == resume_state::finished);
            REQUIRE(operation.result());
            CHECK(actual == expected);
            CHECK(operation.committed_bytes() == wire.size());
        };

        SUBCASE("flat vector") { check(input); }
        SUBCASE("borrowed view of a vector") {
            std::vector<std::ranges::ref_view<std::vector<std::byte>>> chunks{std::views::all(input)};
            check(chunks);
        }
    }

    TEST_CASE_TEMPLATE("flat input growth preserves every split position", Storage, std::vector<std::byte>, std::deque<std::byte>) {
        const std::vector<std::string> expected{"first", std::string(48, 'x'), "last"};
        std::vector<std::byte>         wire;
        REQUIRE(make_encoder(wire)(expected));

        for (std::size_t cut = 0; cut != wire.size(); ++cut) {
            CAPTURE(cut);
            Storage                  input(wire.begin(), wire.begin() + static_cast<std::ptrdiff_t>(cut));
            auto                     decoder = make_resumable_decoder(input);
            std::vector<std::string> actual;
            auto                     operation = decoder(actual);
            REQUIRE_FALSE(operation.done());
            const auto committed = operation.committed_bytes();
            CHECK(operation.resume() == resume_state::need_input);
            CHECK(operation.committed_bytes() == committed);

            // Force relocation for vector, while exercising ordinary deque growth too.
            if constexpr (ReservableStorage<Storage>) {
                input.reserve(input.capacity() + wire.size());
            }
            input.insert(input.end(), wire.begin() + static_cast<std::ptrdiff_t>(cut), wire.end());
            REQUIRE(operation.resume() == resume_state::finished);
            REQUIRE(operation.result());
            CHECK(operation.committed_bytes() == wire.size());
            CHECK(actual == expected);
            CHECK(operation.resume() == resume_state::finished);
        }
    }

    TEST_CASE_TEMPLATE("growable output appends after existing bytes", Storage, std::vector<std::byte>, std::deque<std::byte>) {
        const std::string expected = "a growable destination";
        Storage           output;
        REQUIRE(make_encoder(output)(24));
        const auto prefix_size = output.size();
        auto       encoder     = make_resumable_encoder(output);
        auto       operation   = encoder(expected);
        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(operation.committed_bytes() == output.size() - prefix_size);
        CHECK(*operation.result() == operation.committed_bytes());

        int         prefix{};
        std::string actual;
        REQUIRE(make_decoder(output)(prefix, actual));
        CHECK(prefix == 24);
        CHECK(actual == expected);
    }

    TEST_CASE("validator refreshes relocated contiguous input after every split") {
        const std::vector<std::string> expected{"first", std::string(48, 'v'), "last"};
        std::vector<std::byte>         wire;
        REQUIRE(make_encoder(wire)(expected));

        for (std::size_t cut = 0; cut != wire.size(); ++cut) {
            CAPTURE(cut);
            std::vector<std::byte> input(wire.begin(), wire.begin() + static_cast<std::ptrdiff_t>(cut));
            auto                   source    = make_input_source(input);
            auto                   operation = make_resumable_validator(source);
            REQUIRE_FALSE(operation.done());

            input.reserve(input.capacity() + wire.size());
            input.insert(input.end(), wire.begin() + static_cast<std::ptrdiff_t>(cut), wire.end());
            REQUIRE(operation.finish() == resume_state::finished);
            REQUIRE(operation.result());
            CHECK(operation.committed_bytes() == wire.size());
        }
    }

    TEST_CASE("nested tail growth refreshes relocated inner bytes") {
        const std::string      expected(96, 't');
        std::vector<std::byte> wire;
        REQUIRE(make_encoder(wire)(expected));
        std::vector<std::byte>                                   tail(wire.begin(), wire.begin() + 7);
        std::list<std::ranges::ref_view<std::vector<std::byte>>> chunks;
        chunks.emplace_back(tail);
        auto        decoder = make_resumable_decoder(chunks);
        std::string actual;
        auto        operation = decoder(actual);
        REQUIRE_FALSE(operation.done());
        CHECK(operation.committed_bytes() == tail.size());

        tail.reserve(tail.capacity() + wire.size());
        tail.insert(tail.end(), wire.begin() + 7, wire.end());
        REQUIRE(operation.resume() == resume_state::finished);
        REQUIRE(operation.result());
        CHECK(actual == expected);
        CHECK(operation.committed_bytes() == wire.size());
    }

    TEST_CASE("owning nested vectors survive outer and inner relocation") {
        const std::string      expected(96, 'n');
        std::vector<std::byte> wire;
        REQUIRE(make_encoder(wire)(expected));
        std::vector<std::vector<std::byte>> chunks;
        chunks.reserve(1);
        chunks.emplace_back(wire.begin(), wire.begin() + 3);
        auto        decoder = make_resumable_decoder(chunks);
        std::string actual;
        auto        operation = decoder(actual);
        REQUIRE_FALSE(operation.done());

        chunks.reserve(chunks.capacity() + 2);
        chunks.emplace_back(wire.begin() + 3, wire.begin() + 7);
        CHECK(operation.resume() == resume_state::need_input);
        CHECK(operation.committed_bytes() == 7);

        auto &tail = chunks.back();
        tail.reserve(tail.capacity() + wire.size());
        tail.insert(tail.end(), wire.begin() + 7, wire.end());
        REQUIRE(operation.resume() == resume_state::finished);
        REQUIRE(operation.result());
        CHECK(actual == expected);
        CHECK(operation.committed_bytes() == wire.size());
    }

    TEST_CASE("moving an exhausted owning list rebinds its end boundary") {
        const std::string      expected(32, 'l');
        std::vector<std::byte> wire;
        REQUIRE(make_encoder(wire)(expected));
        for (const std::size_t cut : {std::size_t{0}, std::size_t{3}}) {
            CAPTURE(cut);
            std::vector<movable_node_chunk> chunks;
            chunks.reserve(1);
            chunks.emplace_back(wire.begin(), wire.begin() + static_cast<std::ptrdiff_t>(cut));
            auto        decoder = make_resumable_decoder(chunks);
            std::string actual;
            auto        operation = decoder(actual);
            REQUIRE_FALSE(operation.done());

            const auto *previous_data = chunks.data();
            chunks.reserve(chunks.capacity() + 2);
            REQUIRE(chunks.data() != previous_data);
            chunks.emplace_back(wire.begin() + static_cast<std::ptrdiff_t>(cut), wire.end());
            REQUIRE(operation.resume() == resume_state::finished);
            REQUIRE(operation.result());
            CHECK(actual == expected);
            CHECK(operation.committed_bytes() == wire.size());
        }
    }

    TEST_CASE("nested output tail growth preserves tentative ordinary writes") {
        std::vector<std::byte>                                   tail(1);
        std::list<std::ranges::ref_view<std::vector<std::byte>>> chunks;
        chunks.emplace_back(tail);
        auto encoder   = make_resumable_encoder(chunks);
        auto operation = encoder(65536);
        REQUIRE_FALSE(operation.done());
        CHECK(operation.committed_bytes() == 0);

        tail.resize(16);
        REQUIRE(operation.resume() == resume_state::finished);
        REQUIRE(operation.result());
        auto committed = std::span<const std::byte>{tail}.first(operation.committed_bytes());
        int  actual{};
        REQUIRE(make_decoder(committed)(actual));
        CHECK(actual == 65536);
    }

    TEST_CASE("committed byte counts survive cancellation and terminal truncation") {
        const std::string      expected(32, 'c');
        std::vector<std::byte> wire;
        REQUIRE(make_encoder(wire)(expected));

        for (const bool cancel : {false, true}) {
            std::vector<std::byte> input(wire.begin(), wire.begin() + 5);
            auto                   decoder = make_resumable_decoder(input);
            std::string            actual;
            auto                   operation = decoder(actual);
            REQUIRE_FALSE(operation.done());
            REQUIRE(operation.committed_bytes() == input.size());

            if (cancel) {
                operation.cancel();
            } else {
                operation.finish();
            }
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == (cancel ? status_code::cancelled : status_code::incomplete));
            CHECK(operation.committed_bytes() == input.size());
        }
    }

    TEST_CASE("exclusive acquisition precedes eager customization work") {
        std::list<std::span<std::byte>> chunks;
        auto                            encoder = make_resumable_encoder(chunks);
        int                             calls{};
        const counted_customization     value{&calls};
        auto                            operation = encoder(value);
        REQUIRE_FALSE(operation.done());
        const auto before = calls;

        CHECK_THROWS_AS(encoder(value), std::logic_error);
        CHECK(calls == before);
        CHECK(operation.committed_bytes() == 0);
    }

    TEST_CASE("finishing a completed generator item closes its following request") {
        std::vector<std::byte> input;
        REQUIRE(make_encoder(input)(42));
        auto  source = make_input_source(input);
        auto  items  = make_resumable_generator<int>(source);
        auto &first  = items.next();
        REQUIRE(first.done());
        REQUIRE(first.result());
        CHECK(items.value() == 42);

        CHECK(items.finish() == resume_state::finished);
        auto &end = items.next();
        REQUIRE(end.done());
        REQUIRE(end.result());
        CHECK(*end.result() == 0);
        CHECK(end.committed_bytes() == 0);
        CHECK(items.done());
    }
}

TEST_SUITE("roundtrip/resumable_storage") {
    TEST_CASE_TEMPLATE("public sized source reclaims a nonzero prefix during decoding", Ring, resumable_test::ring_source<false, 11>,
                       resumable_test::ring_source<false, 11, true>) {
        const std::vector<std::string> expected{"first", std::string(96, 'r'), "last"};
        std::vector<std::byte>         wire;
        REQUIRE(make_encoder(wire)(expected));
        Ring                     source;
        std::vector<std::string> actual;
        auto                     operation = make_resumable_decoder(source)(actual);

        for (std::size_t supplied = 0; supplied != wire.size();) {
            const auto count =
                source.append(std::span<const std::byte>{wire}.subspan(supplied, std::min(std::size_t{3}, wire.size() - supplied)));
            REQUIRE(count != 0);
            supplied += count;
            operation.resume();
            CHECK(source.committed_position().offset() == 100 + operation.committed_bytes());
            source.reclaim(source.committed_position().offset());
            CHECK(source.size() <= 11);
        }

        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(operation.committed_bytes() == wire.size());
        CHECK(actual == expected);
        CHECK(source.size() == 0);
    }

    TEST_CASE_TEMPLATE("public sized source preserves a partial typed element across reclamation", Ring,
                       resumable_test::ring_source<false, 11>, resumable_test::ring_source<false, 11, true>) {
        const rfc8746::typed_array<std::uint32_t> expected{std::vector<std::uint32_t>{1, 24, 65536, 123456789}};
        std::vector<std::byte>                    wire;
        REQUIRE(make_encoder<codec::typed_array>(wire)(expected));
        Ring                                source;
        rfc8746::typed_array<std::uint32_t> actual;
        auto                                operation = make_resumable_decoder<codec::typed_array>(source)(actual);

        for (const auto &byte : wire) {
            REQUIRE(source.append(std::span{&byte, 1}) == 1);
            operation.resume();
            source.reclaim(source.committed_position().offset());
        }

        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(operation.committed_bytes() == wire.size());
        CHECK(actual.values() == expected.values());
    }

    TEST_CASE_TEMPLATE("public output source publishes each byte once through partial drains", Ring, resumable_test::ring_source<true, 11>,
                       resumable_test::ring_source<true, 11, true>) {
        const std::vector<std::string> expected{"first", std::string(96, 'w'), "last"};
        Ring                           sink;
        auto                           operation = make_resumable_encoder(sink)(expected);
        std::vector<std::byte>         delivered;
        auto                           published = sink.begin().offset();

        // A partial drain leaves an unsent committed tail while encoding can continue.
        while (!operation.done() || published != sink.committed_position().offset()) {
            const auto count = std::min(std::size_t{3}, sink.committed_position().offset() - published);
            REQUIRE(count != 0);
            auto cursor = sink.begin();
            for (std::size_t index = 0; index != count; ++index, ++cursor) {
                delivered.push_back(*cursor);
            }
            published += count;
            sink.reclaim(published);
            operation.resume();
        }

        REQUIRE(operation.result());
        CHECK(operation.committed_bytes() == delivered.size());
        CHECK(sink.size() == 0);
        std::vector<std::string> actual;
        auto                     decoder = make_decoder(delivered);
        REQUIRE(decoder(actual));
        CHECK(actual == expected);
        CHECK(decoder.tell() == delivered.end());
    }

    TEST_CASE_TEMPLATE("public output source retains a partially written typed element", Ring, resumable_test::ring_source<true, 7>,
                       resumable_test::ring_source<true, 7, true>) {
        const rfc8746::typed_array<std::uint32_t> expected{std::vector<std::uint32_t>{1, 24, 65536, 123456789}};
        Ring                                      sink;
        auto                                      operation = make_resumable_encoder<codec::typed_array>(sink)(expected);
        std::vector<std::byte>                    delivered;
        auto                                      published = sink.begin().offset();
        bool                                      observed_tentative{};

        while (!operation.done() || published != sink.committed_position().offset()) {
            observed_tentative = observed_tentative || sink.position().offset() > sink.committed_position().offset();
            const auto count   = std::min(std::size_t{2}, sink.committed_position().offset() - published);
            REQUIRE(count != 0);
            auto cursor = sink.begin();
            for (std::size_t index = 0; index != count; ++index, ++cursor) {
                delivered.push_back(*cursor);
            }
            published += count;
            sink.reclaim(published);
            operation.resume();
        }

        REQUIRE(operation.result());
        CHECK(observed_tentative);
        CHECK(operation.committed_bytes() == delivered.size());
        rfc8746::typed_array<std::uint32_t> actual;
        auto                                decoder = make_decoder<codec::typed_array>(delivered);
        REQUIRE(decoder(actual));
        CHECK(actual.values() == expected.values());
        CHECK(decoder.tell() == delivered.end());
    }

    TEST_CASE("bounded ordinary retry units require explicit admission handling") {
        const ordinary_item                   expected{std::string(96, 'x')};
        resumable_test::ring_source<true, 11> sink;
        auto                                  operation = make_resumable_encoder(sink)(expected);
        REQUIRE_FALSE(operation.done());
        CHECK(operation.committed_bytes() == 0);
        CHECK(operation.resume() == resume_state::need_output);
        CHECK(operation.committed_bytes() == 0);

        // The caller rejects an indivisible item beyond its retention budget.
        operation.cancel();
        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::cancelled);
        CHECK(sink.position() == sink.committed_position());
    }

    TEST_CASE("a source that reports capacity fails an item it can never hold") {
        SUBCASE("output") {
            const ordinary_item                   expected{std::string(96, 'x')};
            resumable_test::ring_source<true, 11> sink;
            sink.reports_capacity = true;
            auto operation        = make_resumable_encoder(sink)(expected);
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::capacity_exceeded);
            CHECK(operation.committed_bytes() == 0);
            CHECK(sink.position() == sink.committed_position()); // nothing tentative is left
        }

        SUBCASE("input") {
            // A view needs its whole item in the ring at once
            std::vector<std::byte> wire;
            REQUIRE(make_encoder(wire)(std::string(20, 'v')));
            resumable_test::ring_source<false, 11> source;
            source.reports_capacity                = true;
            auto                           decoder = make_resumable_decoder(source);
            decltype(decoder)::tstr_view_t view;
            auto                           operation = decoder(view);
            REQUIRE(source.append(wire) == 11);
            operation.resume();
            REQUIRE(operation.done());
            REQUIRE_FALSE(operation.result());
            CHECK(operation.result().error() == status_code::capacity_exceeded);
            CHECK(operation.committed_bytes() == 0);
        }
    }

    TEST_CASE("capacity reports end exactly the runs that would stall") {
        // The second argument is the largest unit taken whole: a head with its
        // argument, or a typed array's 2-byte tag and 1-byte string head, then 4-byte elements
        sweep_capacity_reports(std::numeric_limits<std::uint64_t>::max(), 9);
        sweep_capacity_reports(-1.5e300, 9);
        sweep_capacity_reports(std::string(300, 's'), 3);
        sweep_capacity_reports(std::vector<std::uint32_t>{1, 24, 65536, 123456789, 0}, 5);
        sweep_capacity_reports(std::map<std::string, std::int64_t>{{"a", -1}, {std::string(30, 'k'), 1LL << 40}}, 9);
        sweep_capacity_reports<codec::typed_array>(rfc8746::typed_array<std::uint32_t>{std::vector<std::uint32_t>{1, 24, 65536, 123456789}},
                                                   4);
    }
}
