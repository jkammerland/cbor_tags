#include "test_util.h"

#include <algorithm>
#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/extensions/cbor_visualization.h>
#include <cbor_tags/extensions/smart_ptr.h>
#include <doctest/doctest.h>
#include <list>
#include <map>
#include <ranges>
#include <set>
#include <unordered_set>
#include <variant>
#include <vector>
#include <version>

#if __has_include(<boost/container/slist.hpp>)
#include <boost/array.hpp>
#include <boost/container/deque.hpp>
#include <boost/container/flat_map.hpp>
#include <boost/container/flat_set.hpp>
#include <boost/container/list.hpp>
#include <boost/container/map.hpp>
#include <boost/container/set.hpp>
#include <boost/container/slist.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/container/stable_vector.hpp>
#include <boost/container/static_vector.hpp>
#include <boost/container/string.hpp>
#include <boost/container/vector.hpp>
#include <boost/move/unique_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/unordered/unordered_map.hpp>
#include <boost/unordered/unordered_set.hpp>
#define CBOR_TAGS_TEST_BOOST_CONTAINERS 1
#endif

#if __has_include(<boost/container/devector.hpp>)
#include <boost/container/devector.hpp>
#define CBOR_TAGS_TEST_BOOST_DEVECTOR 1
#endif

#if __has_include(<boost/unordered/unordered_flat_map.hpp>) && __has_include(<boost/unordered/unordered_flat_set.hpp>) && \
    __has_include(<boost/unordered/unordered_node_map.hpp>) && __has_include(<boost/unordered/unordered_node_set.hpp>)
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#include <boost/unordered/unordered_node_map.hpp>
#include <boost/unordered/unordered_node_set.hpp>
#define CBOR_TAGS_TEST_BOOST_OPEN_UNORDERED 1
#endif

#if __has_include(<boost/container/hub.hpp>) && __has_include(<boost/container/segtor.hpp>)
#include <boost/container/hub.hpp>
#include <boost/container/segtor.hpp>
#define CBOR_TAGS_TEST_BOOST_HUB_SEGTOR 1
#endif

#if defined(__cpp_lib_flat_set) && __cpp_lib_flat_set >= 202207L
#include <flat_set>
#endif
#if defined(__cpp_lib_flat_map) && __cpp_lib_flat_map >= 202207L
#include <flat_map>
#endif
#if defined(__cpp_lib_inplace_vector) && __cpp_lib_inplace_vector >= 202406L
#include <inplace_vector>
#endif
#if defined(__cpp_lib_hive) && __cpp_lib_hive >= 202502L
#include <hive>
#endif

using namespace cbor::tags;

namespace {

template <typename Range> std::vector<int> sorted_values(const Range &range) {
    std::vector<int> result(range.begin(), range.end());
    std::ranges::sort(result);
    return result;
}

template <typename Container> void check_insert_container(const std::vector<int> &expected) {
    for (const auto *wire : {"840301010207", "9f03010102ff07"}) {
        CAPTURE(wire);
        Container output;
        output.insert(9);
        const auto input = to_bytes(wire);
        auto       dec   = make_decoder(input);
        REQUIRE(dec(output));
        CHECK(sorted_values(output) == expected);
        int following{};
        REQUIRE(dec(following));
        CHECK(following == 7);

        std::vector<std::byte> encoded;
        REQUIRE(make_encoder(encoded)(output));
        std::vector<int> copy;
        REQUIRE(make_decoder(encoded)(copy));
        CHECK(sorted_values(copy) == expected);
    }
    for (const auto *wire : {"8007", "9fff07"}) {
        Container  output;
        const auto input = to_bytes(wire);
        auto       dec   = make_decoder(input);
        REQUIRE(dec(output));
        CHECK(output.empty());
        int following{};
        REQUIRE(dec(following));
        CHECK(following == 7);
        std::vector<std::byte> encoded;
        REQUIRE(make_encoder(encoded)(output));
        CHECK(encoded == to_bytes("80"));
    }
    for (const auto *wire : {"98", "811901", "9f1901"}) {
        Container  output;
        const auto input  = to_bytes(wire);
        const auto result = make_decoder(input)(output);
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::incomplete);
    }
    const auto malformed = to_bytes("816178");
    Container  output;
    const auto result = make_decoder(malformed)(output);
    REQUIRE_FALSE(result);
    CHECK(result.error() == status_code::no_match_for_int_on_buffer);
}

template <typename Container> void check_sequence() {
    for (const auto *wire : {"8301020307", "9f010203ff07", "8007", "9fff07"}) {
        CAPTURE(wire);
        Container              output{9};
        const auto             input    = to_bytes(wire);
        const bool             empty    = input[0] == std::byte{0x80} || input[1] == std::byte{0xff};
        const std::vector<int> expected = empty ? std::vector<int>{9} : std::vector<int>{9, 1, 2, 3};
        auto                   dec      = make_decoder(input);
        REQUIRE(dec(output));
        CHECK(std::ranges::equal(output, expected));
        int following{};
        REQUIRE(dec(following));
        CHECK(following == 7);
        std::vector<std::byte> encoded;
        REQUIRE(make_encoder(encoded)(output));
        CHECK(encoded == to_bytes(empty ? "8109" : "8409010203"));
    }
}

template <typename Map> void check_map() {
    for (const auto *wire : {"a20102030407", "bf01020304ff07", "a007", "bfff07"}) {
        CAPTURE(wire);
        const auto               input    = to_bytes(wire);
        const bool               empty    = input[0] == std::byte{0xa0} || input[1] == std::byte{0xff};
        const std::map<int, int> expected = empty ? std::map<int, int>{} : std::map<int, int>{{1, 2}, {3, 4}};
        Map                      output;
        auto                     dec = make_decoder(input);
        REQUIRE(dec(output));
        REQUIRE(output.size() == expected.size());
        for (const auto &[key, value] : expected) {
            REQUIRE(output.find(key) != output.end());
            CHECK(output.find(key)->second == value);
        }
        int following{};
        REQUIRE(dec(following));
        CHECK(following == 7);
        std::vector<std::byte> encoded;
        REQUIRE(make_encoder(encoded)(output));
        REQUIRE_FALSE(encoded.empty());
        CHECK(encoded.front() == (empty ? std::byte{0xa0} : std::byte{0xa2}));
        std::map<int, int> copy;
        REQUIRE(make_decoder(encoded)(copy));
        CHECK(copy == expected);
    }
}

template <typename Input> void check_set_input(const Input &input) {
    std::set<int> output{9};
    auto          dec = make_decoder(input);
    REQUIRE(dec(output));
    CHECK(output == std::set<int>{1, 2, 3, 9});
    int following{};
    REQUIRE(dec(following));
    CHECK(following == 7);
}

} // namespace

TEST_CASE("insert containers consume duplicates independently of destination size") {
    check_insert_container<std::set<int>>({1, 2, 3, 9});
    check_insert_container<std::unordered_set<int>>({1, 2, 3, 9});
    check_insert_container<std::multiset<int>>({1, 1, 2, 3, 9});
    check_insert_container<std::unordered_multiset<int>>({1, 1, 2, 3, 9});
}

TEST_CASE("byte sets retain binary string classification across input layouts") {
    const auto check = [](const auto &input) {
        std::set<std::byte> output{std::byte{9}};
        auto                dec = make_decoder(input);
        REQUIRE(dec(output));
        CHECK(output == std::set<std::byte>{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{9}});
        int following{};
        REQUIRE(dec(following));
        CHECK(following == 7);
        std::vector<std::byte> encoded;
        REQUIRE(make_encoder(encoded)(output));
        CHECK(encoded == to_bytes("4401020309"));
    };
    for (const auto *wire : {"440302020107", "5f420302420201ff07"}) {
        const auto bytes = to_bytes(wire);
        check(bytes);
        const std::list<std::byte> linked(bytes.begin(), bytes.end());
        check(linked);
        const auto unsized = std::ranges::subrange<std::list<std::byte>::const_iterator, std::list<std::byte>::const_iterator,
                                                   std::ranges::subrange_kind::unsized>(linked.begin(), linked.end());
        check(unsized);
    }
}

TEST_CASE("set decoding handles noncontiguous and unsized input and consumed headers") {
    for (const auto *wire : {"840301010207", "9f03010102ff07"}) {
        const auto                 bytes = to_bytes(wire);
        const std::list<std::byte> linked(bytes.begin(), bytes.end());
        check_set_input(bytes);
        check_set_input(linked);
        const auto unsized = std::ranges::subrange<std::list<std::byte>::const_iterator, std::list<std::byte>::const_iterator,
                                                   std::ranges::subrange_kind::unsized>(linked.begin(), linked.end());
        static_assert(!std::ranges::sized_range<decltype(unsized)>);
        check_set_input(unsized);
        std::variant<bool, std::set<int>> value;
        auto                              dec = make_decoder(bytes);
        REQUIRE(dec(value));
        REQUIRE(std::holds_alternative<std::set<int>>(value));
        CHECK(std::get<std::set<int>>(value) == std::set<int>{1, 2, 3});
        int following{};
        REQUIRE(dec(following));
        CHECK(following == 7);
    }
    const auto                 nested = to_bytes("82820102810107");
    std::set<std::vector<int>> output;
    auto                       dec = make_decoder(nested);
    REQUIRE(dec(output));
    CHECK(output == std::set<std::vector<int>>{{1}, {1, 2}});
    int following{};
    REQUIRE(dec(following));
    CHECK(following == 7);
}

#ifdef CBOR_TAGS_TEST_BOOST_CONTAINERS
TEST_CASE("boost sequence and map families keep ordinary wire formats") {
    check_sequence<boost::container::vector<int>>();
    check_sequence<boost::container::deque<int>>();
    check_sequence<boost::container::list<int>>();
    check_sequence<boost::container::stable_vector<int>>();
    check_sequence<boost::container::small_vector<int, 4>>();
    check_sequence<boost::container::static_vector<int, 4>>();
    check_sequence<boost::container::slist<int>>();
    check_map<boost::container::map<int, int>>();
    check_map<boost::container::multimap<int, int>>();
    check_map<boost::container::flat_map<int, int>>();
    check_map<boost::container::flat_multimap<int, int>>();
    check_map<boost::unordered_map<int, int>>();
    check_map<boost::unordered_multimap<int, int>>();
}

TEST_CASE("boost set families retain their duplicate semantics") {
    check_insert_container<boost::container::set<int>>({1, 2, 3, 9});
    check_insert_container<boost::container::flat_set<int>>({1, 2, 3, 9});
    check_insert_container<boost::unordered_set<int>>({1, 2, 3, 9});
    check_insert_container<boost::container::multiset<int>>({1, 1, 2, 3, 9});
    check_insert_container<boost::container::flat_multiset<int>>({1, 1, 2, 3, 9});
    check_insert_container<boost::unordered_multiset<int>>({1, 1, 2, 3, 9});
}

TEST_CASE("boost slist appends successive items without reversing a prefix") {
    const auto                   bytes = to_bytes("8201029f0304ff811901");
    const std::list<std::byte>   linked(bytes.begin(), bytes.end());
    const auto                   input = std::ranges::subrange<std::list<std::byte>::const_iterator, std::list<std::byte>::const_iterator,
                                                               std::ranges::subrange_kind::unsized>(linked.begin(), linked.end());
    boost::container::slist<int> output{9, 8};
    auto                         dec = make_decoder(input);
    REQUIRE(dec(output));
    REQUIRE(dec(output));
    CHECK(std::ranges::equal(output, std::vector<int>{9, 8, 1, 2, 3, 4}));
    const auto result = dec(output);
    REQUIRE_FALSE(result);
    CHECK(result.error() == status_code::incomplete);
    CHECK(std::ranges::equal(output, std::vector<int>{9, 8, 1, 2, 3, 4}));
}

TEST_CASE("boost arrays use fixed array dispatch and CDDL extents") {
    static_assert(IsFixedArray<boost::array<int, 3>>);
    static_assert(!IsAggregate<boost::array<int, 3>>);
    static_assert(!IsTuple<boost::array<int, 3>>);
    const auto           input = to_bytes("8301020307");
    boost::array<int, 3> output{};
    auto                 dec = make_decoder(input);
    REQUIRE(dec(output));
    CHECK(std::ranges::equal(output, std::array{1, 2, 3}));
    int following{};
    REQUIRE(dec(following));
    CHECK(following == 7);
    std::vector<std::byte> encoded;
    REQUIRE(make_encoder(encoded)(output));
    CHECK(encoded == to_bytes("83010203"));
    for (const auto *wire : {"820102", "8401020304", "9f010203ff"}) {
        const auto bad    = to_bytes(wire);
        const auto result = make_decoder(bad)(output);
        REQUIRE_FALSE(result);
        CHECK(result.error() == status_code::unexpected_group_size);
    }
    boost::array<int, 0> empty{};
    const auto           empty_input = to_bytes("8007");
    auto                 empty_dec   = make_decoder(empty_input);
    REQUIRE(empty_dec(empty));
    REQUIRE(empty_dec(following));
    CHECK(following == 7);
    encoded.clear();
    REQUIRE(make_encoder(encoded)(empty));
    CHECK(encoded == to_bytes("80"));
    fmt::memory_buffer schema;
    cddl_schema_to<boost::array<int, 3>>(schema, {.row_options = {.format_by_rows = false}});
    CHECK(fmt::to_string(schema) == "root = [3*3 int]");
    schema.clear();
    cddl_schema_to<boost::array<int, 0>>(schema, {.row_options = {.format_by_rows = false}});
    CHECK(fmt::to_string(schema) == "root = [0*0 int]");

    boost::array<std::byte, 4> fixed_buffer{};
    REQUIRE(make_encoder(fixed_buffer)(output));
    CHECK(std::ranges::equal(fixed_buffer, to_bytes("83010203")));
    boost::array<std::byte, 3> binary{};
    const auto                 binary_wire = to_bytes("43010203");
    REQUIRE(make_decoder(binary_wire)(binary));
    CHECK(std::ranges::equal(binary, to_bytes("010203")));
    boost::array<char, 3> text{};
    const auto            char_array_wire = to_bytes("83010203");
    REQUIRE(make_decoder(char_array_wire)(text));
    CHECK(std::ranges::equal(text, std::array{char{1}, char{2}, char{3}}));
    const auto text_wire   = to_bytes("63616263");
    const auto text_result = make_decoder(text_wire)(text);
    REQUIRE_FALSE(text_result);
    CHECK(text_result.error() == status_code::no_match_for_array_on_buffer);
    encoded.clear();
    REQUIRE(make_encoder(encoded)(text));
    CHECK(encoded == char_array_wire);
    schema.clear();
    cddl_schema_to<bounded_size<boost::array<std::byte, 3>, 0, 4>>(schema, {.row_options = {.format_by_rows = false}});
    CHECK(fmt::to_string(schema) == "root = bstr .size 3");
    schema.clear();
    cddl_schema_to<bounded_size<boost::array<int, 3>, 0, 4>>(schema, {.row_options = {.format_by_rows = false}});
    CHECK(fmt::to_string(schema) == "root = [3*3 int]");
}

TEST_CASE("empty byte ranges encode to a fixed Boost output buffer") {
    boost::array<std::byte, 1>   output{};
    const std::vector<std::byte> empty;
    REQUIRE(make_encoder(output)(empty));
    CHECK(output[0] == std::byte{0x40});
}

TEST_CASE("zero extent byte arrays decode empty strings without accessing storage") {
    const auto input = to_bytes("4007");
    const auto check = [&input](auto &output) {
        auto dec = make_decoder(input);
        REQUIRE(dec(output));
        int following{};
        REQUIRE(dec(following));
        CHECK(following == 7);
    };
    boost::array<std::byte, 0> boost_output{};
    std::array<std::byte, 0>   standard_output{};
    check(boost_output);
    check(standard_output);
}

TEST_CASE("boost text and opt in pointer codecs keep their wire representation") {
    const auto               input = to_bytes("6361626307");
    boost::container::string output;
    auto                     dec = make_decoder(input);
    REQUIRE(dec(output));
    CHECK(output == "abc");
    int following{};
    REQUIRE(dec(following));
    CHECK(following == 7);
    std::vector<std::byte> encoded;
    REQUIRE(make_encoder(encoded)(output));
    CHECK(encoded == to_bytes("63616263"));

    using namespace cbor::tags::ext::smart_ptr;
    boost::movelib::unique_ptr<int> unique(new int(42));
    boost::shared_ptr<int>          shared(new int(23));
    encoded.clear();
    REQUIRE(make_encoder<unique_ptr_codec, shared_ptr_codec>(encoded)(unique, shared));
    CHECK(encoded == to_bytes("182ad81c17"));
    auto                            pointer_dec = make_decoder<unique_ptr_codec, shared_ptr_codec>(encoded);
    boost::movelib::unique_ptr<int> unique_copy;
    boost::shared_ptr<int>          shared_copy;
    REQUIRE(pointer_dec(unique_copy, shared_copy));
    REQUIRE(unique_copy);
    REQUIRE(shared_copy);
    CHECK(*unique_copy == 42);
    CHECK(*shared_copy == 23);
    unique.reset();
    shared.reset();
    encoded.clear();
    REQUIRE(make_encoder<unique_ptr_codec, shared_ptr_codec>(encoded)(unique, shared));
    CHECK(encoded == to_bytes("f6f6"));
    REQUIRE(make_decoder<unique_ptr_codec, shared_ptr_codec>(encoded)(unique_copy, shared_copy));
    CHECK_FALSE(unique_copy);
    CHECK_FALSE(shared_copy);
}
#endif

#ifdef CBOR_TAGS_TEST_BOOST_DEVECTOR
TEST_CASE("boost devector preserves sequence order") { check_sequence<boost::container::devector<int>>(); }
#endif
#ifdef CBOR_TAGS_TEST_BOOST_OPEN_UNORDERED
TEST_CASE("boost open addressing sets and maps interoperate") {
    check_insert_container<boost::unordered_flat_set<int>>({1, 2, 3, 9});
    check_insert_container<boost::unordered_node_set<int>>({1, 2, 3, 9});
    check_map<boost::unordered_flat_map<int, int>>();
    check_map<boost::unordered_node_map<int, int>>();
}
#endif
#ifdef CBOR_TAGS_TEST_BOOST_HUB_SEGTOR
TEST_CASE("boost hub and segtor interoperate") {
    check_insert_container<boost::container::hub<int>>({1, 1, 2, 3, 9});
    check_sequence<boost::container::segtor<int>>();
}
#endif
#if defined(__cpp_lib_flat_set) && __cpp_lib_flat_set >= 202207L
TEST_CASE("standard flat sets retain their duplicate semantics") {
    check_insert_container<std::flat_set<int>>({1, 2, 3, 9});
    check_insert_container<std::flat_multiset<int>>({1, 1, 2, 3, 9});
}
#endif
#if defined(__cpp_lib_flat_map) && __cpp_lib_flat_map >= 202207L
TEST_CASE("standard flat maps interoperate") {
    check_map<std::flat_map<int, int>>();
    check_map<std::flat_multimap<int, int>>();
}
#endif
#if defined(__cpp_lib_inplace_vector) && __cpp_lib_inplace_vector >= 202406L
TEST_CASE("standard inplace vector preserves sequence order") { check_sequence<std::inplace_vector<int, 4>>(); }
#endif
#if defined(__cpp_lib_hive) && __cpp_lib_hive >= 202502L
TEST_CASE("standard hive retains duplicates without requiring insertion order") { check_insert_container<std::hive<int>>({1, 1, 2, 3, 9}); }
#endif
