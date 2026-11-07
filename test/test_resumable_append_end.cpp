#include <array>
#include <cbor_tags/cbor_resumable_source.h>
#include <cstddef>
#include <deque>
#include <doctest/doctest.h>
#include <new>
#include <vector>

namespace {
template <typename Storage> std::vector<unsigned> append_end_values(const Storage &storage) {
    std::vector<unsigned> values;
    for (auto byte : storage)
        values.push_back(static_cast<unsigned>(byte));
    return values;
}

template <typename Storage> Storage append_end_prefix() {
    Storage storage;
    storage.push_back(static_cast<typename Storage::value_type>(10));
    storage.push_back(static_cast<typename Storage::value_type>(11));
    return storage;
}

using byte_vector      = std::vector<std::byte>;
using byte_deque       = std::deque<std::byte>;
using converting_deque = std::deque<unsigned char>;

TEST_SUITE("resumable/source") {
    TEST_CASE_TEMPLATE("growable append preserves prefix and zero writes", Storage, byte_vector, byte_deque, converting_deque) {
        auto storage = append_end_prefix<Storage>();
        auto source  = cbor::tags::make_output_source(storage);
        CHECK(source.size() == 2);
        CHECK(source.committed_position().offset() == 2);
        CHECK(source.write({}) == 0);

        std::array<std::byte, 37> input;
        std::vector<unsigned>     expected{10, 11};
        for (unsigned i = 0; i < input.size(); ++i) {
            input[i] = static_cast<std::byte>(i + 20);
            expected.push_back(i + 20);
        }

        CHECK(source.write(input) == 37);
        CHECK(append_end_values(storage) == expected);
        CHECK(source.position().offset() == 39);
        CHECK(source.committed_position().offset() == 2);

        source.commit();
        CHECK(source.committed_position().offset() == 39);
        CHECK(source.write({}) == 0);
        CHECK(append_end_values(storage) == expected);
        CHECK(source.size() == 39);
        CHECK(source.committed_position().offset() == 39);

        Storage empty;
        auto    fresh = cbor::tags::make_output_source(empty);
        CHECK(fresh.write({}) == 0);
        CHECK(empty.empty());
        const std::array one{std::byte{7}};
        CHECK(fresh.write(one) == 1);
        CHECK(append_end_values(empty) == std::vector<unsigned>{7});
        CHECK(fresh.position().offset() == 1);
        CHECK(fresh.committed_position().offset() == 0);
    }

    TEST_CASE_TEMPLATE("growable rollback overwrites then appends only the remainder", Storage, byte_vector, byte_deque, converting_deque) {
        auto             storage    = append_end_prefix<Storage>();
        auto             source     = cbor::tags::make_output_source(storage);
        const auto       checkpoint = source.position();
        const std::array initial{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
        REQUIRE(source.write(initial) == 4);
        CHECK(source.size() == 6);
        CHECK(source.committed_position().offset() == 2);

        // A short retry overwrites only the tentative prefix.
        source.restore(checkpoint);
        CHECK(source.size() == 2);
        const std::array shorter{std::byte{8}, std::byte{9}};
        CHECK(source.write(shorter) == 2);
        CHECK(append_end_values(storage) == std::vector<unsigned>{10, 11, 8, 9, 3, 4});
        CHECK(source.size() == 4);
        CHECK(source.committed_position().offset() == 2);

        source.restore(checkpoint);
        const std::array exact{std::byte{5}, std::byte{6}, std::byte{7}, std::byte{8}};
        CHECK(source.write(exact) == 4);
        CHECK(append_end_values(storage) == std::vector<unsigned>{10, 11, 5, 6, 7, 8});
        CHECK(source.size() == 6);
        CHECK(source.committed_position().offset() == 2);

        // Crossing the old extent appends only the remaining bytes.
        source.restore(checkpoint);
        const std::array across{std::byte{20}, std::byte{21}, std::byte{22}, std::byte{23}, std::byte{24}, std::byte{25}};
        CHECK(source.write(across) == 6);
        CHECK(append_end_values(storage) == std::vector<unsigned>{10, 11, 20, 21, 22, 23, 24, 25});
        CHECK(source.size() == 8);
        CHECK(source.committed_position().offset() == 2);

        source.commit();
        const std::array next{std::byte{26}};
        CHECK(source.write(next) == 1);
        CHECK(append_end_values(storage) == std::vector<unsigned>{10, 11, 20, 21, 22, 23, 24, 25, 26});
        CHECK(source.size() == 9);
        CHECK(source.committed_position().offset() == 8);
    }

    TEST_CASE_TEMPLATE("closed growable output permits overwrite but never growth", Storage, byte_vector, byte_deque, converting_deque) {
        auto             storage    = append_end_prefix<Storage>();
        auto             source     = cbor::tags::make_output_source(storage);
        const auto       checkpoint = source.position();
        const std::array initial{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
        REQUIRE(source.write(initial) == 4);

        source.close();
        CHECK(source.closed());
        CHECK(source.write({}) == 0);
        CHECK(source.write(initial) == 0);
        CHECK(append_end_values(storage) == std::vector<unsigned>{10, 11, 1, 2, 3, 4});
        CHECK(source.size() == 6);
        CHECK(source.committed_position().offset() == 2);

        // Closure still permits overwriting admitted tentative storage.
        source.restore(checkpoint);
        const std::array replacement{std::byte{30}, std::byte{31}, std::byte{32}, std::byte{33}, std::byte{34}, std::byte{35}};
        CHECK(source.write(replacement) == 4);
        CHECK(append_end_values(storage) == std::vector<unsigned>{10, 11, 30, 31, 32, 33});
        CHECK(source.size() == 6);
        CHECK(source.committed_position().offset() == 2);
        CHECK(source.write({}) == 0);
        CHECK(source.write(initial) == 0);

        source.commit();
        CHECK(source.committed_position().offset() == 6);
        CHECK(append_end_values(storage) == std::vector<unsigned>{10, 11, 30, 31, 32, 33});
    }

    struct partially_throwing_bytes : std::deque<unsigned char> {
        int remaining = -1;

        void push_back(unsigned char byte) {
            if (remaining == 0)
                throw std::bad_alloc{};
            std::deque<unsigned char>::push_back(byte);
            if (remaining > 0)
                --remaining;
        }
    };

    TEST_CASE("partial converting append recovers through explicit refresh and restore") {
        auto       storage    = append_end_prefix<partially_throwing_bytes>();
        auto       source     = cbor::tags::make_output_source(storage);
        const auto checkpoint = source.position();

        // Fail after physical growth, before the source publishes the new extent.
        storage.remaining = 2;
        const std::array initial{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
        CHECK_THROWS_AS(source.write(initial), std::bad_alloc);
        CHECK(append_end_values(storage) == std::vector<unsigned>{10, 11, 1, 2});
        CHECK(source.size() == 2);
        CHECK(source.committed_position().offset() == 2);

        // Recovery explicitly admits that growth before retrying from the checkpoint.
        storage.remaining = -1;
        source.refresh();
        source.restore(checkpoint);
        const std::array replacement{std::byte{9}, std::byte{8}, std::byte{7}, std::byte{6}, std::byte{5}};
        CHECK(source.write(replacement) == 5);
        CHECK(append_end_values(storage) == std::vector<unsigned>{10, 11, 9, 8, 7, 6, 5});
        CHECK(source.size() == 7);
        CHECK(source.committed_position().offset() == 2);
        source.commit();
        CHECK(source.committed_position().offset() == 7);
    }
} // TEST_SUITE
} // namespace
