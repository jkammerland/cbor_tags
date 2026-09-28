#include "test_util.h"

#include <array>
#include <cbor_tags/cbor_traversal.h>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <list>
#include <new>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace cbor::tags;

namespace {

template <typename T> std::string event_name(const T &value) {
    if constexpr (std::same_as<T, as_array_any> || std::same_as<T, as_map_any> || std::same_as<T, as_text_any> ||
                  std::same_as<T, as_bstr_any>) {
        std::string kind;
        if constexpr (std::same_as<T, as_array_any>) {
            kind = "array";
        } else if constexpr (std::same_as<T, as_map_any>) {
            kind = "map";
        } else if constexpr (std::same_as<T, as_text_any>) {
            kind = "text";
        } else {
            kind = "bytes";
        }
        return kind + "(" + std::to_string(value.size) + (value.indefinite ? ",indef)" : ",def)");
    } else if constexpr (std::same_as<T, as_tag_any>) {
        return "tag(" + std::to_string(value.tag) + ")";
    } else if constexpr (std::same_as<T, positive>) {
        return "uint(" + std::to_string(value) + ")";
    } else if constexpr (std::ranges::range<T>) {
        return "payload";
    } else {
        return "scalar";
    }
}

std::string event_kind_name(walk_event_kind kind) {
    switch (kind) {
    case walk_event_kind::enter: return "enter";
    case walk_event_kind::value: return "value";
    case walk_event_kind::leave: return "leave";
    }
    return "unknown";
}

struct CountingUnsizedTraversalRange {
    using value_type = std::byte;

    struct iterator {
        using value_type        = std::byte;
        using difference_type   = std::ptrdiff_t;
        using iterator_concept  = std::bidirectional_iterator_tag;
        using iterator_category = std::bidirectional_iterator_tag;

        const value_type *current{};
        std::size_t      *increments{};

        const value_type &operator*() const { return *current; }
        iterator         &operator++() {
            ++current;
            ++*increments;
            return *this;
        }
        iterator operator++(int) {
            auto old = *this;
            ++*this;
            return old;
        }
        iterator &operator--() {
            --current;
            return *this;
        }
        iterator operator--(int) {
            auto old = *this;
            --*this;
            return old;
        }
        friend bool operator==(const iterator &, const iterator &) = default;
    };

    std::vector<std::byte> bytes;
    mutable std::size_t    increments{};
    iterator               begin() const { return {bytes.data(), &increments}; }
    iterator               end() const { return {bytes.data() + bytes.size(), &increments}; }
};

static_assert(std::ranges::bidirectional_range<CountingUnsizedTraversalRange>);
static_assert(!std::ranges::sized_range<CountingUnsizedTraversalRange>);
static_assert(!std::ranges::contiguous_range<CountingUnsizedTraversalRange>);

struct ThrowingUtf8TraversalRange {
    using value_type = std::byte;

    struct iterator {
        using value_type        = std::byte;
        using difference_type   = std::ptrdiff_t;
        using iterator_concept  = std::bidirectional_iterator_tag;
        using iterator_category = std::bidirectional_iterator_tag;

        const value_type                 *current{};
        const ThrowingUtf8TraversalRange *owner{};

        const value_type &operator*() const {
            // The header remains readable. Decoding the borrowed payload only
            // advances iterators; strict UTF-8 validation reads this byte later.
            if (current == owner->bytes.data() + 1) {
                owner->payload_dereferenced = true;
                if (owner->allocation_failure) {
                    throw std::bad_alloc{};
                }
                throw std::runtime_error{"payload iterator failure"};
            }
            return *current;
        }
        iterator &operator++() {
            ++current;
            return *this;
        }
        iterator operator++(int) {
            auto old = *this;
            ++*this;
            return old;
        }
        iterator &operator--() {
            --current;
            return *this;
        }
        iterator operator--(int) {
            auto old = *this;
            --*this;
            return old;
        }
        friend bool operator==(const iterator &, const iterator &) = default;
    };

    std::vector<std::byte> bytes;
    bool                   allocation_failure{};
    mutable bool           payload_dereferenced{};
    iterator               begin() const { return {bytes.data(), this}; }
    iterator               end() const { return {bytes.data() + bytes.size(), this}; }
};

static_assert(CborInputBuffer<ThrowingUtf8TraversalRange>);
static_assert(std::ranges::bidirectional_range<ThrowingUtf8TraversalRange>);
static_assert(!std::ranges::sized_range<ThrowingUtf8TraversalRange>);
static_assert(!std::ranges::contiguous_range<ThrowingUtf8TraversalRange>);

} // namespace

TEST_SUITE("cbor_wire/traversal") {

    TEST_CASE("nested headers payloads and closing breaks report their original source and depth") {
        const auto input = to_bytes("9fa101d864826268694200ffbf027f606161ffff5f404101ffff07");
        const auto check = [](const auto &buffer) {
            auto                     dec = make_decoder(buffer);
            std::vector<std::string> events;
            REQUIRE(walk_item(dec, [&](const auto &value, const auto &context) {
                static_assert(!std::same_as<std::remove_cvref_t<decltype(value)>, indefinite_break>);
                CHECK(context.source.end() == dec.tell());
                events.push_back(event_kind_name(context.kind) + "/" + std::to_string(context.depth) + "/" + event_name(value) + "/" +
                                 to_hex(context.source));
            }));
            const std::vector<std::string> expected{
                "enter/0/array(0,indef)/9f", "enter/1/map(1,def)/a1",     "value/2/uint(1)/01",      "enter/2/tag(100)/d864",
                "enter/3/array(2,def)/82",   "enter/4/text(2,def)/62",    "value/4/payload/6869",    "leave/4/text(2,def)/",
                "enter/4/bytes(2,def)/42",   "value/4/payload/00ff",      "leave/4/bytes(2,def)/",   "leave/3/array(2,def)/",
                "leave/2/tag(100)/",         "leave/1/map(1,def)/",       "enter/1/map(0,indef)/bf", "value/2/uint(2)/02",
                "enter/2/text(0,indef)/7f",  "enter/3/text(0,def)/60",    "value/3/payload/",        "leave/3/text(0,def)/",
                "enter/3/text(1,def)/61",    "value/3/payload/61",        "leave/3/text(1,def)/",    "leave/2/text(0,indef)/ff",
                "leave/1/map(0,indef)/ff",   "enter/1/bytes(0,indef)/5f", "enter/2/bytes(0,def)/40", "value/2/payload/",
                "leave/2/bytes(0,def)/",     "enter/2/bytes(1,def)/41",   "value/2/payload/01",      "leave/2/bytes(1,def)/",
                "leave/1/bytes(0,indef)/ff", "leave/0/array(0,indef)/ff",
            };
            REQUIRE_EQ(events.size(), expected.size());
            for (std::size_t index = 0; index < expected.size(); ++index) {
                CAPTURE(index);
                CHECK_EQ(events[index], expected[index]);
            }
            CHECK(dec.tell() == std::prev(buffer.end()));
            positive trailing{};
            REQUIRE(dec(trailing));
            CHECK_EQ(trailing, 7);
            CHECK(dec.tell() == buffer.end());
        };
        check(input);
        check(std::deque<std::byte>{input.begin(), input.end()});
        check(std::list<std::byte>{input.begin(), input.end()});
    }

    TEST_CASE("empty indefinite strings have no payload and nonminimal headers keep their original bytes") {
        for (const auto hex : {"5fff", "7fff"}) {
            const auto                   input = to_bytes(hex);
            auto                         dec   = make_decoder(input);
            std::vector<walk_event_kind> kinds;
            REQUIRE(walk_item(dec, [&](const auto &, const auto &context) { kinds.push_back(context.kind); }));
            CHECK(kinds == std::vector<walk_event_kind>{walk_event_kind::enter, walk_event_kind::leave});
        }
        for (const auto hex : {"780168", "59000168"}) {
            const auto               input = to_bytes(hex);
            auto                     dec   = make_decoder(input);
            std::vector<std::string> sources;
            REQUIRE(walk_item(dec, [&](const auto &, const auto &context) { sources.push_back(to_hex(context.source)); }));
            REQUIRE_EQ(sources.size(), 3);
            CHECK_EQ(sources.front(), std::string(hex).substr(0, std::string(hex).size() - 2));
            CHECK_EQ(sources[1], "68");
            CHECK(sources.back().empty());
        }
    }

    TEST_CASE("payload views borrow the original contiguous deque and list storage") {
        auto check = []<typename Input>(Input input) {
            auto dec         = make_decoder(input);
            using text_type  = decltype(dec.decode_text_payload(0));
            using bytes_type = decltype(dec.decode_bstring_payload(0));
            std::optional<text_type>  text;
            std::optional<bytes_type> bytes;
            REQUIRE(walk_item(dec, [&](const auto &value, const auto &context) {
                using T = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::same_as<T, text_type>) {
                    CHECK(context.kind == walk_event_kind::value);
                    text = value;
                    if constexpr (std::same_as<T, std::string_view>) {
                        CHECK(reinterpret_cast<const std::byte *>(value.data()) == std::addressof(*context.source.begin()));
                    } else {
                        CHECK(value.range.begin() == context.source.begin());
                        CHECK(value.range.end() == context.source.end());
                    }
                } else if constexpr (std::same_as<T, bytes_type>) {
                    CHECK(context.kind == walk_event_kind::value);
                    bytes = value;
                    if constexpr (std::same_as<T, std::span<const std::byte>>) {
                        CHECK(value.data() == std::addressof(*context.source.begin()));
                    } else {
                        CHECK(value.range.begin() == context.source.begin());
                        CHECK(value.range.end() == context.source.end());
                    }
                }
            }));
            REQUIRE(text.has_value());
            REQUIRE(bytes.has_value());
            CHECK_EQ(std::string(text->begin(), text->end()), "hi");
            CHECK(to_hex(*bytes) == "00ff");
            *std::next(input.begin(), 2) = std::byte{'j'};
            *std::next(input.begin(), 5) = std::byte{0xaa};
            CHECK_EQ(std::string(text->begin(), text->end()), "ji");
            CHECK_EQ(to_hex(*bytes), "aaff");
            CHECK(dec.tell() == std::prev(input.cend()));
        };
        const auto input = to_bytes("826268694200ff07");
        check(input);
        check(std::deque<std::byte>{input.begin(), input.end()});
        check(std::list<std::byte>{input.begin(), input.end()});
    }

    TEST_CASE("unsized traversal consumes once without walking trailing input to establish availability") {
        for (const auto hex : {"01", "8201626162", "a1014200ff", "5f410140ff"}) {
            auto       bytes     = to_bytes(hex);
            const auto root_size = bytes.size();
            bytes.resize(root_size + 4096, std::byte{0});
            const CountingUnsizedTraversalRange input{bytes};
            auto                                dec = make_decoder(input);
            REQUIRE(walk_item(dec, [](const auto &, const auto &) {}));
            CHECK_EQ(input.increments, root_size);
            CHECK(dec.tell().current == input.bytes.data() + root_size);
        }
        const CountingUnsizedTraversalRange incomplete{to_bytes("430102")};
        auto                                dec    = make_decoder(incomplete);
        const auto                          result = walk_item(dec, [](const auto &, const auto &) {});
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::incomplete);
        CHECK_EQ(incomplete.increments, 3);
        CHECK(dec.tell() == incomplete.end());

        const CountingUnsizedTraversalRange valid{to_bytes("a1014200ff07")};
        auto                                validator = make_decoder(valid);
        REQUIRE(validate_item(validator));
        CHECK_EQ(valid.increments, 5);
        CHECK(validator.tell().current == valid.bytes.data() + 5);
    }

    TEST_CASE("malformed structures fail and truncated roots report incomplete") {
        struct malformed_case {
            const char *hex;
            status_code status;
        };
        const std::array cases{
            malformed_case{"", status_code::incomplete},
            malformed_case{"18", status_code::incomplete},
            malformed_case{"1a0000", status_code::incomplete},
            malformed_case{"6268", status_code::incomplete},
            malformed_case{"4201", status_code::incomplete},
            malformed_case{"81", status_code::incomplete},
            malformed_case{"a101", status_code::incomplete},
            malformed_case{"9f", status_code::incomplete},
            malformed_case{"9f01", status_code::incomplete},
            malformed_case{"bf01", status_code::incomplete},
            malformed_case{"7f6161", status_code::incomplete},
            malformed_case{"5f4101", status_code::incomplete},
            malformed_case{"c0", status_code::incomplete},
            malformed_case{"ff", status_code::malformed_structure},
            malformed_case{"81ff", status_code::malformed_structure},
            malformed_case{"a101ff", status_code::malformed_structure},
            malformed_case{"bf01ff", status_code::malformed_structure},
            malformed_case{"c0ff", status_code::malformed_structure},
            malformed_case{"7f4101ff", status_code::malformed_structure},
            malformed_case{"5f6161ff", status_code::malformed_structure},
            malformed_case{"5f5fffff", status_code::malformed_structure},
            malformed_case{"7f7fffff", status_code::malformed_structure},
            malformed_case{"7f01ff", status_code::malformed_structure},
            malformed_case{"1c", status_code::invalid_additional_info},
            malformed_case{"7c", status_code::invalid_additional_info},
            malformed_case{"fc", status_code::invalid_additional_info},
        };
        for (const auto &test : cases) {
            CAPTURE(test.hex);
            const auto input = to_bytes(test.hex);
            const auto check = [&](const auto &buffer) {
                auto       dec  = make_decoder(buffer);
                const auto walk = walk_item(dec, [](const auto &, const auto &) {});
                REQUIRE_FALSE(walk);
                CHECK(walk.error() == test.status);
                auto       validation_dec = make_decoder(buffer);
                const auto validation     = validate_item(validation_dec);
                REQUIRE_FALSE(validation);
                CHECK(validation.error() == test.status);
            };
            check(input);
            check(std::list<std::byte>{input.begin(), input.end()});
        }
    }

    TEST_CASE("huge declared lengths and counts stop immediately at missing content") {
        // String lengths fit 32-bit input size types; container counts exercise
        // the full unsigned domain without overflowing a map pair count.
        for (const auto hex : {"5a7fffffff", "7a7fffffff", "9bffffffffffffffff", "bbffffffffffffffff"}) {
            CAPTURE(hex);
            const auto input = to_bytes(hex);
            const auto check = [](const auto &buffer) {
                auto       dec    = make_decoder(buffer);
                bool       began  = false;
                const auto result = walk_item(dec, [&](const auto &, const auto &context) {
                    CHECK(context.kind == walk_event_kind::enter);
                    began = true;
                });
                REQUIRE_FALSE(result);
                CHECK(result.error() == status_code::incomplete);
                CHECK(began);
                CHECK(dec.tell() == std::ranges::end(buffer));
                auto       validation_dec = make_decoder(buffer);
                const auto validation     = validate_item(validation_dec);
                REQUIRE_FALSE(validation);
                CHECK(validation.error() == status_code::incomplete);
                CHECK(validation_dec.tell() == std::ranges::end(buffer));
            };
            check(input);
            check(std::list<std::byte>{input.begin(), input.end()});
            check(CountingUnsizedTraversalRange{input});
        }
    }

    TEST_CASE("strict validation rejects extended simple values below 32 only") {
        for (unsigned payload = 0; payload <= 255; ++payload) {
            CAPTURE(payload);
            const std::vector<std::uint8_t> input{0xf8, static_cast<std::uint8_t>(payload), 0x07};
            auto                            permissive_dec = make_decoder(input);
            REQUIRE(walk_item(permissive_dec, [](const auto &, const auto &) {}));
            CHECK(permissive_dec.tell() == input.cbegin() + 2);
            auto        strict_dec       = make_decoder(input);
            std::size_t strict_callbacks = 0;
            const auto strict = walk_item(strict_dec, [&](const auto &, const auto &) { ++strict_callbacks; }, {.strict_validation = true});
            auto       validation_dec = make_decoder(input);
            const auto validation     = validate_item(validation_dec);
            if (payload < 32) {
                REQUIRE_FALSE(strict);
                CHECK(strict.error() == status_code::malformed_structure);
                CHECK_EQ(strict_callbacks, 0);
                REQUIRE_FALSE(validation);
                CHECK(validation.error() == status_code::malformed_structure);
            } else {
                CHECK(strict.has_value());
                CHECK_EQ(strict_callbacks, 1);
                CHECK(validation.has_value());
            }
        }
    }

    TEST_CASE("strict validation checks UTF-8 inside each definite text chunk") {
        for (const auto hex : {"60", "63616263", "62c3a9", "63e282ac", "64f0908d88", "7f6062c3a9ff", "1801", "190001"}) {
            CAPTURE(hex);
            const auto input = to_bytes(hex);
            auto       dec   = make_decoder(input);
            REQUIRE(validate_item(dec));
        }
        for (const auto hex : {"61ff", "6180", "61c3", "62c080", "63eda080", "64f4908080", "7f61c361a9ff"}) {
            CAPTURE(hex);
            const auto input = to_bytes(hex);
            const auto check = [](const auto &buffer) {
                auto permissive_dec = make_decoder(buffer);
                REQUIRE(walk_item(permissive_dec, [](const auto &, const auto &) {}));
                auto       strict_dec = make_decoder(buffer);
                bool       began      = false;
                const auto strict     = walk_item(strict_dec,
                                                  [&](const auto &, const auto &context) {
                                                  CHECK(context.kind == walk_event_kind::enter);
                                                  began = true;
                                                  },
                                                  {.strict_validation = true});
                REQUIRE_FALSE(strict);
                CHECK(strict.error() == status_code::invalid_utf8_sequence);
                CHECK(began);
                auto       validation_dec = make_decoder(buffer);
                const auto validation     = validate_item(validation_dec);
                REQUIRE_FALSE(validation);
                CHECK(validation.error() == status_code::invalid_utf8_sequence);
            };
            check(input);
            check(std::list<std::byte>{input.begin(), input.end()});
        }
    }

    TEST_CASE("strict validation converts payload iterator exceptions after advancing the decoder") {
        for (const bool allocation_failure : {false, true}) {
            CAPTURE(allocation_failure);
            const ThrowingUtf8TraversalRange input{to_bytes("6161"), allocation_failure};
            const auto                       expected_status = allocation_failure ? status_code::out_of_memory : status_code::error;

            auto permissive_dec = make_decoder(input);
            bool payload_seen   = false;
            REQUIRE(walk_item(permissive_dec,
                              [&](const auto &, const auto &context) { payload_seen |= context.kind == walk_event_kind::value; }));
            CHECK(payload_seen);
            CHECK_FALSE(input.payload_dereferenced);
            CHECK(permissive_dec.tell() == input.end());

            auto       strict_dec = make_decoder(input);
            bool       began      = false;
            const auto strict     = walk_item(strict_dec,
                                              [&](const auto &, const auto &context) {
                                              CHECK(context.kind == walk_event_kind::enter);
                                              began = true;
                                              },
                                              {.strict_validation = true});
            REQUIRE_FALSE(strict);
            CHECK(strict.error() == expected_status);
            CHECK(began);
            CHECK(input.payload_dereferenced);
            CHECK(strict_dec.tell() == input.end());

            input.payload_dereferenced = false;
            auto       validation_dec  = make_decoder(input);
            const auto validation      = validate_item(validation_dec);
            REQUIRE_FALSE(validation);
            CHECK(validation.error() == expected_status);
            CHECK(input.payload_dereferenced);
            CHECK(validation_dec.tell() == input.end());
        }
    }

    TEST_CASE("depth limits apply to headers while scalar children and payloads stay within the owning frame") {
        for (const auto hex : {"00", "20", "f4", "f6", "f820", "fa3f800000"}) {
            const auto input = to_bytes(hex);
            auto       dec   = make_decoder(input);
            REQUIRE(walk_item(dec, [](const auto &, const auto &) {}, {.max_depth = 0}));
        }
        struct depth_case {
            const char *hex;
            std::size_t required_depth;
        };
        const std::array cases{
            depth_case{"80", 1},     depth_case{"a0", 1},     depth_case{"60", 1},       depth_case{"40", 1},       depth_case{"6100", 1},
            depth_case{"4100", 1},   depth_case{"8101", 1},   depth_case{"a10102", 1},   depth_case{"c001", 1},     depth_case{"9fff", 1},
            depth_case{"bfff", 1},   depth_case{"7fff", 1},   depth_case{"5fff", 1},     depth_case{"8180", 2},     depth_case{"c080", 2},
            depth_case{"7f60ff", 2}, depth_case{"5f40ff", 2}, depth_case{"9f9fffff", 2}, depth_case{"bfa0a0ff", 2},
        };
        for (const auto &test : cases) {
            CAPTURE(test.hex);
            const auto input = to_bytes(test.hex);
            auto       dec   = make_decoder(input);
            REQUIRE(walk_item(dec, [](const auto &, const auto &) {}, {.max_depth = test.required_depth}));
            auto       limited_dec = make_decoder(input);
            const auto limited     = walk_item(limited_dec, [](const auto &, const auto &) {}, {.max_depth = test.required_depth - 1});
            REQUIRE_FALSE(limited);
            CHECK(limited.error() == status_code::size_limit_exceeded);
            auto       validation_dec = make_decoder(input);
            const auto validation     = validate_item(validation_dec, {.max_depth = test.required_depth - 1});
            REQUIRE_FALSE(validation);
            CHECK(validation.error() == status_code::size_limit_exceeded);
        }
    }

    TEST_CASE("integer callbacks preserve the full positive and negative CBOR domains") {
        const auto            input = to_bytes("841bffffffffffffffff203b7fffffffffffffff3bffffffffffffffff07");
        auto                  dec   = make_decoder(input);
        std::vector<positive> positives;
        std::vector<negative> negatives;
        REQUIRE(walk_item(dec, [&](const auto &value, const auto &context) {
            using T = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<T, positive>) {
                CHECK(context.kind == walk_event_kind::value);
                positives.push_back(value);
            } else if constexpr (std::same_as<T, negative>) {
                CHECK(context.kind == walk_event_kind::value);
                negatives.push_back(value);
            }
        }));
        CHECK(positives == std::vector<positive>{std::numeric_limits<std::uint64_t>::max()});
        CHECK(negatives == std::vector<negative>{negative{1}, negative{std::uint64_t{1} << 63}, negative{0}});
        CHECK(dec.tell() == std::prev(input.cend()));
    }

    TEST_CASE("validation consumes one complete root and leaves malformed trailing input untouched") {
        const auto input = to_bytes("8101ff");
        auto       dec   = make_decoder(input);
        REQUIRE(validate_item(dec));
        CHECK(dec.tell() == input.cbegin() + 2);
        const auto trailing = validate_item(dec);
        REQUIRE_FALSE(trailing);
        CHECK(trailing.error() == status_code::malformed_structure);
        CHECK(dec.tell() == input.cend());
    }

    TEST_CASE("callback rejection is terminal without consuming the remaining array elements") {
        const auto                   input = to_bytes("82010207");
        auto                         dec   = make_decoder(input);
        std::vector<walk_event_kind> kinds;
        const auto                   result = walk_item(dec, [&](const auto &, const auto &context) {
            kinds.push_back(context.kind);
            return context.kind == walk_event_kind::value ? status_code::no_match_for_tag : status_code::success;
        });
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::no_match_for_tag);
        CHECK(kinds == std::vector<walk_event_kind>{walk_event_kind::enter, walk_event_kind::value});
        CHECK(dec.tell() == input.cbegin() + 2);
        positive remaining{};
        REQUIRE(dec(remaining));
        CHECK_EQ(remaining, 2);
    }

} // TEST_SUITE("cbor_wire/traversal")
