#include "expected_test_types.h"

#include <cbor_tags/detail/expected.h>
#include <doctest/doctest.h>
#include <memory>
#include <ostream>
#include <type_traits>
#include <utility>

namespace ex = cbor::tags::detail::expected_impl;
using namespace expected_test;

TEST_SUITE("expected/contract") {
    TEST_CASE("monadic operations forward all four value and error reference categories") {
        ex::expected<int, int>       value(5);
        const ex::expected<int, int> constant(5);
        CHECK(*value.transform(qualifier{}) == 1);
        CHECK(*constant.transform(qualifier{}) == 2);
        CHECK(*std::move(value).transform(qualifier{}) == 3);
        CHECK(*std::move(constant).transform(qualifier{}) == 4);
        auto bind = [](auto &&n) { return ex::expected<int, int>(qualifier{}(std::forward<decltype(n)>(n))); };
        CHECK(*value.and_then(bind) == 1);
        CHECK(*constant.and_then(bind) == 2);
        CHECK(*std::move(value).and_then(bind) == 3);
        CHECK(*std::move(constant).and_then(bind) == 4);
        ex::expected<int, int>       error(ex::unexpect, 7);
        const ex::expected<int, int> const_error(ex::unexpect, 7);
        CHECK(error.transform_error(qualifier{}).error() == 1);
        CHECK(const_error.transform_error(qualifier{}).error() == 2);
        CHECK(std::move(error).transform_error(qualifier{}).error() == 3);
        CHECK(std::move(const_error).transform_error(qualifier{}).error() == 4);
        CHECK(*error.or_else(bind) == 1);
        CHECK(*const_error.or_else(bind) == 2);
        CHECK(*std::move(error).or_else(bind) == 3);
        CHECK(*std::move(const_error).or_else(bind) == 4);
        CHECK(*value.transform(rvalue_callback{}) == 6);
    }

    TEST_CASE("monadic callbacks run only for the matching alternative") {
        int                    calls = 0;
        ex::expected<int, int> value(2), error(ex::unexpect, 7);
        auto                   bind = [&](int n) {
            ++calls;
            return ex::expected<int, int>(n + 1);
        };
        CHECK(*value.and_then(bind) == 3);
        CHECK(error.and_then(bind).error() == 7);
        CHECK(calls == 1);
        CHECK(*value.or_else(bind) == 2);
        CHECK(*error.or_else(bind) == 8);
        CHECK(calls == 2);
        auto transform = [&](int n) {
            ++calls;
            return n + 10;
        };
        CHECK(*value.transform(transform) == 12);
        CHECK(error.transform(transform).error() == 7);
        CHECK(calls == 3);
        CHECK(*value.transform_error(transform) == 2);
        CHECK(error.transform_error(transform).error() == 17);
        CHECK(calls == 4);
    }

    TEST_CASE("void monadic operations preserve success and forward errors") {
        ex::expected<void, int>       value, error(ex::unexpect, 7);
        const ex::expected<void, int> constant, const_error(ex::unexpect, 7);
        int                           calls = 0;
        auto                          bind  = [&] {
            ++calls;
            return ex::expected<int, int>(2);
        };
        CHECK(*value.and_then(bind) == 2);
        CHECK(*constant.and_then(bind) == 2);
        CHECK(*std::move(value).and_then(bind) == 2);
        CHECK(*std::move(constant).and_then(bind) == 2);
        CHECK(error.and_then(bind).error() == 7);
        CHECK(const_error.and_then(bind).error() == 7);
        CHECK(std::move(error).and_then(bind).error() == 7);
        CHECK(std::move(const_error).and_then(bind).error() == 7);
        CHECK(calls == 4);
        auto recover = [](auto &&n) { return ex::expected<void, int>(ex::unexpect, qualifier{}(std::forward<decltype(n)>(n))); };
        CHECK(error.or_else(recover).error() == 1);
        CHECK(const_error.or_else(recover).error() == 2);
        CHECK(std::move(error).or_else(recover).error() == 3);
        CHECK(std::move(const_error).or_else(recover).error() == 4);
        CHECK(value.or_else(recover).has_value());
        CHECK(constant.or_else(recover).has_value());
        CHECK(std::move(value).or_else(recover).has_value());
        CHECK(std::move(constant).or_else(recover).has_value());
        CHECK(error.transform_error(qualifier{}).error() == 1);
        CHECK(const_error.transform_error(qualifier{}).error() == 2);
        CHECK(std::move(error).transform_error(qualifier{}).error() == 3);
        CHECK(std::move(const_error).transform_error(qualifier{}).error() == 4);
        auto transform = [&] {
            ++calls;
            return 3;
        };
        CHECK(*value.transform(transform) == 3);
        CHECK(*constant.transform(transform) == 3);
        CHECK(*std::move(value).transform(transform) == 3);
        CHECK(*std::move(constant).transform(transform) == 3);
        CHECK(error.transform(transform).error() == 7);
        CHECK(const_error.transform(transform).error() == 7);
        CHECK(std::move(error).transform(transform).error() == 7);
        CHECK(std::move(const_error).transform(transform).error() == 7);
        CHECK(calls == 8);
    }

    TEST_CASE("transform supports void and constructs immovable results in place") {
        ex::expected<int, int> value(7);
        int                    calls     = 0;
        auto                   discarded = value.transform([&](int n) {
            ++calls;
            CHECK(n == 7);
        });
        static_assert(std::is_same_v<decltype(discarded), ex::expected<void, int>>);
        CHECK(discarded.has_value());
        CHECK(calls == 1);
        auto fixed = value.transform([](int n) { return immovable(n); });
        CHECK(fixed->number == 7);
        auto fixed_error = ex::expected<int, int>(ex::unexpect, 9).transform_error([](int n) { return immovable(n); });
        CHECK(fixed_error.error().number == 9);
        ex::expected<void, int> empty;
        auto                    empty_fixed = empty.transform([] { return immovable(11); });
        CHECK(empty_fixed->number == 11);
        auto empty_discarded = empty.transform([&] { ++calls; });
        CHECK(empty_discarded.has_value());
        CHECK(calls == 2);
        auto void_fixed_error = ex::expected<void, int>(ex::unexpect, 12).transform_error([](int n) { return immovable(n); });
        CHECK(void_fixed_error.error().number == 12);
        auto success_with_immovable_error = empty.transform_error([](int n) { return immovable(n); });
        CHECK(success_with_immovable_error.has_value());
        auto failure_with_immovable_value = ex::expected<int, int>(ex::unexpect, 13).transform([](int n) { return immovable(n); });
        CHECK(failure_with_immovable_value.error() == 13);
    }

    TEST_CASE("monadic invocation accepts member pointers and returns expected references") {
        struct item {
            int number;
            int get() const { return number + 1; }
        };
        ex::expected<item, int> value(std::in_place, 4);
        CHECK(*value.transform(&item::get) == 5);
        // A member object pointer returns a reference, which transform intentionally rejects.
        ex::expected<int, int> destination(6);
        auto                   copied = value.and_then([&](const item &) -> ex::expected<int, int>                   &{ return destination; });
        CHECK(*copied == 6);
        ex::expected<int, int> error(ex::unexpect, 7);
        auto                   recovered = error.or_else([&](int) -> const ex::expected<int, int>                   &{ return destination; });
        CHECK(*recovered == 6);
        auto owned_callback = [owned = std::make_unique<int>(8)](int n) { return n + *owned; };
        CHECK(*destination.transform(std::move(owned_callback)) == 14);
    }

    TEST_CASE("callback exceptions propagate and leave the source alternative alive") {
        ex::expected<int, int> value(3), error(ex::unexpect, 7);
        CHECK_THROWS_AS(value.transform([](int) -> int { throw failure{}; }), failure);
        CHECK_THROWS_AS(value.and_then([](int) -> ex::expected<int, int> { throw failure{}; }), failure);
        CHECK_THROWS_AS(error.transform_error([](int) -> int { throw failure{}; }), failure);
        CHECK_THROWS_AS(error.or_else([](int) -> ex::expected<int, int> { throw failure{}; }), failure);
        CHECK(*value == 3);
        CHECK(error.error() == 7);
    }
}
