#include <cbor_tags/cbor_resumable.h>
#include <doctest/doctest.h>
#include <list>
#include <map>
#include <span>
#include <vector>

using namespace cbor::tags;

namespace {

using input_chunks = std::list<std::span<const std::byte>>;

std::vector<std::byte> wire(std::initializer_list<unsigned> values) {
    std::vector<std::byte> result;
    for (const auto value : values)
        result.push_back(static_cast<std::byte>(value));
    return result;
}

template <typename T>
void check_leaf_failure(const std::vector<std::byte> &bytes, const T &prefix, status_code expected_status, std::size_t committed) {
    for (std::size_t cut = 0; cut <= bytes.size(); ++cut) {
        CAPTURE(cut);
        const std::span<const std::byte> input{bytes};
        input_chunks                     storage{input.first(cut), input.subspan(cut)};
        auto                             source = make_input_source(storage);
        T                                actual;
        auto                             operation = make_resumable_decoder(source)(actual);
        if (!operation.done())
            operation.finish();
        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == expected_status);
        CHECK(actual == prefix);
        CHECK(source.committed_position().offset() == committed);

        // Every uncommitted byte, including the rejected child's header, remains
        // visible after a terminal error; an incomplete scalar is retried whole.
        std::vector<std::byte> tail;
        for (const auto byte : operation.remaining_input())
            tail.push_back(static_cast<std::byte>(byte));
        CHECK(tail == std::vector<std::byte>(bytes.begin() + static_cast<std::ptrdiff_t>(committed), bytes.end()));
    }
}

} // namespace

TEST_SUITE("cbor_wire/resumable") {

    TEST_CASE("bounded float drains reject mismatched widths before touching their payload") {
        check_leaf_failure(wire({0x82, 0xfa, 0x3f, 0x80, 0, 0, 0xfb, 0x3f, 0xf0, 0, 0, 0, 0, 0, 0, 0x09}), std::vector<float>{1.0F},
                           status_code::no_match_for_tag_simple_on_buffer, 6);
        check_leaf_failure(wire({0x82, 0xfb, 0x3f, 0xf0, 0, 0, 0, 0, 0, 0, 0xfa, 0x3f, 0x80, 0, 0, 0x09}), std::vector<double>{1.0},
                           status_code::no_match_for_tag_simple_on_buffer, 10);
        check_leaf_failure(wire({0x9f, 0xfa, 0x3f, 0x80, 0, 0, 0xf9, 0x3c, 0, 0xff, 0x09}), std::vector<float>{1.0F},
                           status_code::no_match_for_tag_simple_on_buffer, 6);
        check_leaf_failure(wire({0x82, 0xfa, 0x3f, 0x80, 0, 0, 0x1b, 0, 0, 0, 0, 0, 0, 0, 1, 0x09}), std::vector<float>{1.0F},
                           status_code::no_match_for_simple_on_buffer, 6);
    }

    TEST_CASE("bounded bool drains preserve malformed item status and array prefixes") {
        check_leaf_failure(wire({0x83, 0xf5, 0xf4, 0xf8, 0x14, 0x09, 0x09}), std::vector<bool>{true, false},
                           status_code::no_match_for_tag_simple_on_buffer, 3);
        check_leaf_failure(wire({0x9f, 0xf5, 0x01, 0xff, 0x09}), std::vector<bool>{true}, status_code::no_match_for_simple_on_buffer, 2);
    }

    TEST_CASE("bounded map failures retain completed keys without appending an unfinished pair") {
        check_leaf_failure(wire({0xa1, 0xfa, 0x3f, 0x80, 0, 0, 0xf6, 0x09, 0x09}), std::map<float, bool>{},
                           status_code::no_match_for_tag_simple_on_buffer, 6);
        check_leaf_failure(wire({0xbf, 0xf4, 0xfb, 0x3f, 0xf0, 0, 0, 0, 0, 0, 0, 0xf5, 0xff, 0x09}), std::map<bool, double>{{false, 1.0}},
                           status_code::malformed_structure, 12);
    }

    TEST_CASE("bounded float drains keep incomplete scalars at the last complete child") {
        check_leaf_failure(wire({0x82, 0xfa, 0x3f, 0x80, 0, 0, 0xfa, 0x40, 0}), std::vector<float>{1.0F}, status_code::incomplete, 6);
        check_leaf_failure(wire({0xbf, 0xf5, 0xfb, 0x40, 0}), std::map<bool, double>{}, status_code::incomplete, 2);
    }

    TEST_CASE("bounded leaf success leaves the following item untouched") {
        const auto        bytes = wire({0x9f, 0xf5, 0xf4, 0xff, 0x09});
        input_chunks      storage{std::span<const std::byte>{bytes}};
        auto              source = make_input_source(storage);
        std::vector<bool> actual;
        {
            auto operation = make_resumable_decoder(source)(actual);
            REQUIRE(operation.done());
            REQUIRE(operation.result());
            CHECK(*operation.result() == 4U);
            CHECK(actual == std::vector<bool>{true, false});
        }
        unsigned tail{};
        auto     following = make_resumable_decoder(source)(tail);
        REQUIRE(following.done());
        REQUIRE(following.result());
        CHECK(*following.result() == 1U);
        CHECK(tail == 9U);
    }
}
