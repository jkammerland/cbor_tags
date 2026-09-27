#include "container_interoperability_fixtures.h"

using namespace cbor_container_test;

namespace {

template <typename Container> void check_insert_container(const std::vector<int> &expected) {
    const std::vector<int> input{3, 1, 1, 2};
    std::vector<std::byte> encoded;
    REQUIRE(make_encoder(encoded)(input));
    Container output;
    output.insert(9);
    REQUIRE(make_decoder(encoded)(output));
    CHECK(sorted_values(output) == expected);
    encoded.clear();
    REQUIRE(make_encoder(encoded)(output));
    std::vector<int> copy;
    REQUIRE(make_decoder(encoded)(copy));
    CHECK(sorted_values(copy) == expected);

    const Container empty;
    encoded.clear();
    REQUIRE(make_encoder(encoded)(empty));
    Container empty_copy;
    REQUIRE(make_decoder(encoded)(empty_copy));
    CHECK(empty_copy.empty());
}

template <typename Container> void check_sequence() {
    for (const auto &input : {std::vector<int>{1, 2, 3}, std::vector<int>{}}) {
        std::vector<std::byte> encoded;
        REQUIRE(make_encoder(encoded)(input));
        Container output{9};
        REQUIRE(make_decoder(encoded)(output));
        auto expected = std::vector<int>{9};
        expected.insert(expected.end(), input.begin(), input.end());
        CHECK(std::ranges::equal(output, expected));
        encoded.clear();
        REQUIRE(make_encoder(encoded)(output));
        std::vector<int> copy;
        REQUIRE(make_decoder(encoded)(copy));
        CHECK(copy == expected);
    }
}

template <typename Map> void check_map() {
    for (const auto &expected : {std::map<int, int>{{1, 2}, {3, 4}}, std::map<int, int>{}}) {
        std::vector<std::byte> encoded;
        REQUIRE(make_encoder(encoded)(expected));
        Map output;
        REQUIRE(make_decoder(encoded)(output));
        REQUIRE(output.size() == expected.size());
        for (const auto &[key, value] : expected) {
            REQUIRE(output.find(key) != output.end());
            CHECK(output.find(key)->second == value);
        }
        encoded.clear();
        REQUIRE(make_encoder(encoded)(output));
        std::map<int, int> copy;
        REQUIRE(make_decoder(encoded)(copy));
        CHECK(copy == expected);
    }
}

} // namespace

TEST_SUITE("roundtrip/container_interoperability") {

    TEST_CASE("insert containers preserve values and duplicate semantics") {
        check_insert_container<std::set<int>>({1, 2, 3, 9});
        check_insert_container<std::unordered_set<int>>({1, 2, 3, 9});
        check_insert_container<std::multiset<int>>({1, 1, 2, 3, 9});
        check_insert_container<std::unordered_multiset<int>>({1, 1, 2, 3, 9});
    }

#ifdef CBOR_TAGS_TEST_BOOST_CONTAINERS
    TEST_CASE("boost sequence and map families round trip values") {
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

    TEST_CASE("boost text arrays and pointers round trip values") {
        const boost::container::string text("abc");
        const boost::array<int, 3>     values{{1, 2, 3}};
        std::vector<std::byte>         encoded;
        REQUIRE(make_encoder(encoded)(text, values));
        boost::container::string text_copy;
        boost::array<int, 3>     values_copy{};
        REQUIRE(make_decoder(encoded)(text_copy, values_copy));
        CHECK(text_copy == text);
        CHECK(values_copy == values);

        using namespace cbor::tags::ext::smart_ptr;
        boost::movelib::unique_ptr<int> unique(new int(42));
        boost::shared_ptr<int>          shared(new int(23));
        encoded.clear();
        REQUIRE(make_encoder<unique_ptr_codec, shared_ptr_codec>(encoded)(unique, shared));
        boost::movelib::unique_ptr<int> unique_copy;
        boost::shared_ptr<int>          shared_copy;
        REQUIRE(make_decoder<unique_ptr_codec, shared_ptr_codec>(encoded)(unique_copy, shared_copy));
        REQUIRE(unique_copy);
        REQUIRE(shared_copy);
        CHECK(*unique_copy == *unique);
        CHECK(*shared_copy == *shared);
        unique.reset();
        shared.reset();
        encoded.clear();
        REQUIRE(make_encoder<unique_ptr_codec, shared_ptr_codec>(encoded)(unique, shared));
        REQUIRE(make_decoder<unique_ptr_codec, shared_ptr_codec>(encoded)(unique_copy, shared_copy));
        CHECK_FALSE(unique_copy);
        CHECK_FALSE(shared_copy);
    }
#endif

#ifdef CBOR_TAGS_TEST_BOOST_DEVECTOR
    TEST_CASE("boost devector preserves sequence order") { check_sequence<boost::container::devector<int>>(); }
#endif
#ifdef CBOR_TAGS_TEST_BOOST_OPEN_UNORDERED
    TEST_CASE("boost open addressing sets and maps round trip") {
        check_insert_container<boost::unordered_flat_set<int>>({1, 2, 3, 9});
        check_insert_container<boost::unordered_node_set<int>>({1, 2, 3, 9});
        check_map<boost::unordered_flat_map<int, int>>();
        check_map<boost::unordered_node_map<int, int>>();
    }
#endif
#ifdef CBOR_TAGS_TEST_BOOST_HUB_SEGTOR
    TEST_CASE("boost hub and segtor round trip") {
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
    TEST_CASE("standard flat maps round trip") {
        check_map<std::flat_map<int, int>>();
        check_map<std::flat_multimap<int, int>>();
    }
#endif
#if defined(__cpp_lib_inplace_vector) && __cpp_lib_inplace_vector >= 202406L
    TEST_CASE("standard inplace vector preserves sequence order") { check_sequence<std::inplace_vector<int, 4>>(); }
#endif
#if defined(__cpp_lib_hive) && __cpp_lib_hive >= 202502L
    TEST_CASE("standard hive retains duplicates without requiring insertion order") {
        check_insert_container<std::hive<int>>({1, 1, 2, 3, 9});
    }
#endif

} // TEST_SUITE
