#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>

#if CBOR_TAGS_HAS_NAMED_REFLECTION

#include <algorithm>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace cbor::tags;

namespace {

using input_chunks = std::list<std::span<const std::byte>>;

struct named_station {
    std::string                name;
    std::vector<std::uint32_t> values;
    std::optional<std::string> note;
    int                        id;

    bool operator==(const named_station &) const = default;
};

struct named_position {
    double lat;
    double lon;
};

struct named_reading {
    std::string                    sensor;
    as_named_group<named_position> position;
    std::optional<std::uint32_t>   battery;
};

bool same_reading(const named_reading &lhs, const named_reading &rhs) {
    return lhs.sensor == rhs.sensor && lhs.position.value_.lat == rhs.position.value_.lat &&
           lhs.position.value_.lon == rhs.position.value_.lon && lhs.battery == rhs.battery;
}

struct named_extended {
    std::string                                            id;
    as_named_extension<std::map<std::string, std::string>> extra;
};

bool same_extended(const named_extended &lhs, const named_extended &rhs) { return lhs.id == rhs.id && lhs.extra.value_ == rhs.extra.value_; }

// The one-shot decoder is the reference: every input split must give its
// status, and on success the same value.
template <typename T, typename Equal> void check_named_map_splits(std::span<const std::byte> wire, Equal equal) {
    T          expected{};
    const auto expected_result = make_decoder(wire)(as_named_map{expected});

    for (std::size_t cut = 0; cut <= wire.size(); ++cut) {
        CAPTURE(cut);
        input_chunks storage;
        auto         source = make_input_source(storage);
        T            actual{};
        auto         operation = make_resumable_decoder(source)(as_named_map{actual});
        storage.emplace_back(wire.first(cut));
        operation.resume();
        storage.emplace_back(wire.subspan(cut));
        operation.resume();
        if (!operation.done())
            operation.close_input();

        REQUIRE(operation.done());
        const auto result = operation.result();
        REQUIRE(result.has_value() == expected_result.has_value());
        if (expected_result) {
            CHECK(*result == wire.size());
            CHECK(equal(actual, expected));
        } else {
            CHECK(result.error() == expected_result.error());
        }
    }

    // One byte per resume: every field and element boundary suspends.
    std::vector<std::byte> input;
    auto                   decoder = make_resumable_decoder(input);
    T                      actual{};
    auto                   operation = decoder(as_named_map{actual});
    for (const auto byte : wire) {
        if (operation.done())
            break;
        input.push_back(byte);
        operation.resume();
    }
    if (!operation.done())
        operation.close_input();

    REQUIRE(operation.done());
    const auto result = operation.result();
    REQUIRE(result.has_value() == expected_result.has_value());
    if (expected_result)
        CHECK(equal(actual, expected));
    else
        CHECK(result.error() == expected_result.error());
}

std::string repeat(std::string_view hex, std::size_t count) {
    std::string result;
    for (std::size_t index = 0; index < count; ++index)
        result += hex;
    return result;
}

template <typename T> std::vector<std::byte> encode_named(const T &value) {
    std::vector<std::byte> wire;
    REQUIRE(make_encoder(wire)(as_named_map{value}));
    return wire;
}

} // namespace

TEST_SUITE("cbor_wire/resumable") {

    TEST_CASE("named maps resume per entry without repeating completed fields") {
        const named_station sent{
            .name   = std::string(100, 'n'),
            .values = {1, 10, 1000, 100000},
            .note   = std::string(40, 'x'),
            .id     = 7,
        };
        const auto wire = encode_named(sent);

        check_named_map_splits<named_station>(wire, std::equal_to<>{});

        // The original failure: pieces smaller than a field duplicated it.
        std::vector<std::byte> input;
        auto                   decoder = make_resumable_decoder(input);
        named_station          received{};
        auto                   operation = decoder(as_named_map{received});
        for (std::size_t offset = 0; offset < wire.size(); offset += 16) {
            const auto count = std::min<std::size_t>(16, wire.size() - offset);
            input.insert(input.end(), wire.begin() + static_cast<std::ptrdiff_t>(offset),
                         wire.begin() + static_cast<std::ptrdiff_t>(offset + count));
            operation.resume();
        }
        REQUIRE(operation.done());
        REQUIRE(operation.result());
        CHECK(received.name.size() == 100);
        CHECK(received == sent);
    }

    TEST_CASE("named maps resume flattened groups, optionals and extension fields") {
        const named_reading reading{
            .sensor   = "thermo",
            .position = as_named_group<named_position>{named_position{.lat = 59.3, .lon = 18.1}},
            .battery  = 87,
        };
        check_named_map_splits<named_reading>(encode_named(reading), same_reading);

        named_extended extended{.id = "device"};
        extended.extra.value_ = {{"k1", "v1"}, {"k2", std::string(30, 'v')}};
        check_named_map_splits<named_extended>(encode_named(extended), same_extended);
    }

    TEST_CASE("named maps report the one-shot status for malformed entries at every split") {
        // Entries reused below: "name": "a", "values": [], "id": 1.
        const std::string name_a    = "646e616d65" "6161";
        const std::string no_values = "6676616c756573" "80";
        const std::string id_1      = "626964" "01";

        const std::string cases[] = {
            "a1" "637a7a7a" "01",                                   // unknown key "zzz"
            "a4" + name_a + no_values + id_1 + "626964" "02",       // duplicate "id"
            "a1" + id_1,                                            // missing required fields
            "a4" + name_a + no_values + id_1 + "646e6f7465" "f6",   // explicit null for optional "note"
            "80",                                                   // not a map
            "a1" "01" "01",                                         // integer key
            "bf" + name_a + "6676616c756573" "8101" + id_1 + "ff",  // indefinite map
            "a1" "74" + repeat("6b", 20) + "01",                    // 20-byte key, longer than every field
        };
        for (const auto &hex : cases) {
            CAPTURE(hex);
            const auto wire = to_bytes(hex);
            REQUIRE_FALSE(wire.empty());
            check_named_map_splits<named_station>(wire, std::equal_to<>{});
        }
    }

    TEST_CASE("named map keys that can't match fail before their payload arrives") {
        // A key header declaring 1 GiB. Waiting for that payload, or reserving
        // it, would be pointless: no field name is that long.
        std::vector<std::byte> input = to_bytes("a17a40000000");
        auto                   decoder = make_resumable_decoder(input);
        named_station          received{};
        auto                   operation = decoder(as_named_map{received});
        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::unexpected_group_size);
    }

    TEST_CASE("named maps encode per entry with the one-shot bytes at every output split") {
        named_station station{.name = std::string(100, 'n'), .values = {1, 10, 1000}, .note = std::nullopt, .id = 7};
        named_extended extended{.id = "device"};
        extended.extra.value_ = {{"k1", "v1"}, {"k2", std::string(30, 'v')}};
        const named_reading reading{
            .sensor   = "thermo",
            .position = as_named_group<named_position>{named_position{.lat = 59.3, .lon = 18.1}},
            .battery  = 87,
        };

        auto check = [](const auto &value) {
            const auto expected = encode_named(value);
            for (std::size_t cut = 0; cut <= expected.size(); ++cut) {
                CAPTURE(cut);
                std::vector<std::byte>          storage(expected.size());
                std::list<std::span<std::byte>> output;
                auto                            encoder   = make_resumable_encoder(output);
                auto                            operation = encoder(as_named_map{value});
                output.push_back(std::span{storage}.first(cut));
                operation.resume();
                output.push_back(std::span{storage}.subspan(cut));
                operation.resume();

                REQUIRE(operation.done());
                REQUIRE(operation.result());
                CHECK(operation.committed_bytes() == expected.size());
                CHECK(storage == expected);
            }
        };
        check(station);
        check(extended);
        check(reading);

        // Completed entries commit while the rest waits for output space.
        // Retrying the whole map would commit nothing until all of it fits.
        const auto                      expected = encode_named(station);
        std::vector<std::byte>          storage(expected.size());
        std::list<std::span<std::byte>> output;
        auto                            encoder   = make_resumable_encoder(output);
        auto                            operation = encoder(as_named_map{station});
        output.push_back(std::span{storage}.first(16));
        CHECK(operation.resume() == resume_state::need_output);
        CHECK(operation.committed_bytes() > 0);
        CHECK(operation.committed_bytes() <= 16);
    }

    TEST_CASE("named map encoding rejects a shadowing extension key before writing") {
        named_extended extended{.id = "device"};
        extended.extra.value_ = {{"id", "shadow"}};

        std::vector<std::byte> one_shot;
        const auto             expected = make_encoder(one_shot)(as_named_map{extended});
        REQUIRE_FALSE(expected);

        std::vector<std::byte> output;
        auto                   encoder   = make_resumable_encoder(output);
        auto                   operation = encoder(as_named_map{extended});
        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == expected.error());
        CHECK(output.empty());
    }

    TEST_CASE("named map extension values with duplicate keys are rejected at every split") {
        // {"id": "a", "k": "v", "k": "w"}
        check_named_map_splits<named_extended>(to_bytes("a3" "6269646161" "616b6176" "616b6177"), same_extended);
    }
}

#endif
