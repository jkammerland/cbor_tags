#include "expected_test_types.h"

#include <array>
#include <cbor_tags/detail/expected.h>
#include <doctest/doctest.h>
#include <type_traits>
#include <utility>

namespace ex = cbor::tags::detail::expected_impl;

namespace {
enum class scalar_error : unsigned char { before = 7, after = 11 };

template <typename Value> constexpr bool scalar_assignment(Value before, Value after) {
    using result    = ex::expected<Value, scalar_error>;
    const auto make = [](bool success, Value value, scalar_error error) { return success ? result(value) : result(ex::unexpect, error); };
    for (bool move : {false, true}) {
        for (bool old_success : {false, true}) {
            for (bool new_success : {false, true}) {
                auto  left = make(old_success, before, scalar_error::before), right = make(new_success, after, scalar_error::after);
                auto *assigned = move ? &(left = std::move(right)) : &(left = std::as_const(right));
                if (assigned != &left || left.has_value() != new_success || right.has_value() != new_success)
                    return false;
                if (new_success ? (*left != after || *right != after)
                                : (left.error() != scalar_error::after || right.error() != scalar_error::after))
                    return false;
                left        = std::as_const(left);
                auto &alias = left;
                left        = std::move(alias);
                if (new_success ? *left != after : left.error() != scalar_error::after)
                    return false;
            }
        }
    }
    return true;
}
constexpr bool pointer_assignment() {
    int before{}, after{};
    return scalar_assignment(&before, &after);
}
static_assert(scalar_assignment(0x100000003ULL, 0x200000007ULL));
static_assert(pointer_assignment());
static_assert(!std::is_trivially_copy_assignable_v<ex::expected<unsigned long long, scalar_error>>);
static_assert(!std::is_trivially_move_assignable_v<ex::expected<void, scalar_error>>);

struct record {
    std::array<int, 3> numbers;
    const int         *address;
};

constexpr bool trivial_object_assignment() {
    int marker{};
    using result    = ex::expected<record, std::array<int, 2>>;
    const auto make = [&](bool success, int n) {
        if (success)
            return result(std::in_place, record{{n, n + 1, n + 2}, &marker});
        return result(ex::unexpect, std::array{n, n + 1});
    };
    const auto matches = [&](const result &r, bool success, int n) {
        if (r.has_value() != success)
            return false;
        if (success)
            return r->numbers == std::array{n, n + 1, n + 2} && r->address == &marker;
        return r.error() == std::array{n, n + 1};
    };
    for (bool move : {false, true}) {
        for (bool old_success : {false, true}) {
            for (bool new_success : {false, true}) {
                auto  left = make(old_success, 3), right = make(new_success, 7);
                auto *assigned = move ? &(left = std::move(right)) : &(left = std::as_const(right));
                if (assigned != &left || !matches(left, new_success, 7) || !matches(right, new_success, 7))
                    return false;
                left        = std::as_const(left);
                auto &alias = left;
                left        = std::move(alias);
                if (!matches(left, new_success, 7))
                    return false;
            }
        }
    }
    return true;
}

template <typename Void, typename Error> constexpr bool void_assignment(Error before, Error after) {
    using result    = ex::expected<Void, Error>;
    const auto make = [](bool success, Error error) {
        if (success)
            return result();
        return result(ex::unexpect, error);
    };
    for (bool move : {false, true}) {
        for (bool old_success : {false, true}) {
            for (bool new_success : {false, true}) {
                auto  left = make(old_success, before), right = make(new_success, after);
                auto *assigned = move ? &(left = std::move(right)) : &(left = std::as_const(right));
                if (assigned != &left || left.has_value() != new_success || right.has_value() != new_success)
                    return false;
                if (!new_success && (left.error() != after || right.error() != after))
                    return false;
            }
        }
    }
    return true;
}
static_assert(trivial_object_assignment());
static_assert(void_assignment<void>(std::array{3, 4}, std::array{7, 8}));
static_assert(void_assignment<const void>(scalar_error::before, scalar_error::after));
static_assert(!std::is_trivially_copy_assignable_v<ex::expected<record, int>>);
static_assert(!std::is_trivially_move_assignable_v<ex::expected<record, int>>);

enum class effect { copy, move, destroy };
template <effect Effect> struct observed {
    int *events;
    int  number;
    constexpr observed(int &events, int number) noexcept : events(&events), number(number) {}
    observed(const observed &)
        requires(Effect != effect::copy)
    = default;
    constexpr observed(const observed &other)
        requires(Effect == effect::copy)
        : events(other.events), number(other.number) {
        ++*events;
    }
    observed(observed &&)
        requires(Effect != effect::move)
    = default;
    constexpr observed(observed &&other) noexcept
        requires(Effect == effect::move)
        : events(other.events), number(other.number) {
        ++*events;
    }
    observed &operator=(const observed &) = default;
    observed &operator=(observed &&)      = default;
    ~observed()
        requires(Effect != effect::destroy)
    = default;
    constexpr ~observed()
        requires(Effect == effect::destroy)
    {
        ++*events;
    }
};

template <effect Effect> void state_change_effects() {
    using payload = observed<Effect>;
    static_assert(std::is_trivially_copy_assignable_v<payload> && std::is_trivially_move_assignable_v<payload>);
    int                        events{};
    ex::expected<payload, int> value(std::in_place, events, 42), error(ex::unexpect, 7);
    if constexpr (Effect == effect::destroy) {
        value = error;
        CHECK(events == 1);
        CHECK(value.error() == 7);
    } else {
        if constexpr (Effect == effect::copy)
            error = std::as_const(value);
        else
            error = std::move(value);
        REQUIRE(error.has_value());
        CHECK(error->number == 42);
        CHECK(events == 1);
    }
    // An error payload needs the same lifetime rules, including void results.
    events = 0;
    ex::expected<void, payload> failed(ex::unexpect, events, 13), empty;
    if constexpr (Effect == effect::destroy) {
        failed = empty;
        CHECK(failed.has_value());
    } else {
        if constexpr (Effect == effect::copy)
            empty = std::as_const(failed);
        else
            empty = std::move(failed);
        REQUIRE_FALSE(empty.has_value());
        CHECK(empty.error().number == 13);
    }
    CHECK(events == 1);
}
} // namespace

TEST_SUITE("expected/contract") {
    TEST_CASE("scalar assignment preserves numbers and pointers across both states") {
        CHECK(scalar_assignment(0x100000003ULL, 0x200000007ULL));
        CHECK(pointer_assignment());
    }

    TEST_CASE("trivial payload assignment transfers nested objects across both states") {
        CHECK(trivial_object_assignment());
        CHECK(void_assignment<void>(std::array{3, 4}, std::array{7, 8}));
        CHECK(void_assignment<const void>(std::array{3, 4}, std::array{7, 8}));
        CHECK(void_assignment<void>(scalar_error::before, scalar_error::after));
        CHECK(void_assignment<const void>(scalar_error::before, scalar_error::after));
    }

    TEST_CASE("trivial payload assignment retains nontrivial construction and destruction") {
        state_change_effects<effect::copy>();
        state_change_effects<effect::move>();
        state_change_effects<effect::destroy>();
    }

    TEST_CASE("failed bad access construction propagates payload exceptions without leaking") {
        expected_test::counts counts;
        {
            using error_type = expected_test::tracked<false>;
            ex::expected<int, error_type>  error(ex::unexpect, counts, 42);
            ex::expected<void, error_type> empty_error(ex::unexpect, counts, 13);
            counts.fail_copy = true;
            CHECK_THROWS_AS(error.value(), expected_test::failure);
            CHECK_THROWS_AS(std::as_const(empty_error).value(), expected_test::failure);
            counts.fail_copy = false;
            counts.fail_move = true;
            CHECK_THROWS_AS(error.value(), expected_test::failure);
            CHECK_THROWS_AS(std::move(error).value(), expected_test::failure);
            CHECK_THROWS_AS(std::move(empty_error).value(), expected_test::failure);
            REQUIRE_FALSE(error.has_value());
            REQUIRE_FALSE(empty_error.has_value());
            CHECK(error.error().number == 42);
            CHECK(empty_error.error().number == 13);
            CHECK(counts.live == 2);
        }
        CHECK(counts.live == 0);
    }
}
