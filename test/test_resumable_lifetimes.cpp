#include "test_util.h"

#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <functional>
#include <list>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace cbor::tags;

namespace {

using input_chunks     = std::list<std::span<const std::byte>>;
using output_chunks    = std::list<std::span<std::byte>>;
using operation_result = expected<std::size_t, status_code>;

// Own the consumer frame independently from the operation being awaited.
class raw_consumer {
  public:
    struct promise_type {
        std::exception_ptr failure;

        raw_consumer        get_return_object() { return raw_consumer{std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_never  initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void                return_void() noexcept {}
        void                unhandled_exception() noexcept { failure = std::current_exception(); }
    };

    explicit raw_consumer(std::coroutine_handle<promise_type> handle) : handle_(handle) {}
    raw_consumer(const raw_consumer &)            = delete;
    raw_consumer &operator=(const raw_consumer &) = delete;
    raw_consumer(raw_consumer &&other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    ~raw_consumer() {
        if (handle_) {
            handle_.destroy();
        }
    }

    bool done() const { return handle_.done(); }
    void rethrow() const {
        if (handle_.promise().failure) {
            std::rethrow_exception(handle_.promise().failure);
        }
    }

  private:
    std::coroutine_handle<promise_type> handle_;
};

template <typename Operation, typename Callback>
raw_consumer await_result(Operation &operation, std::optional<operation_result> &result, Callback callback) {
    result.emplace(co_await operation);
    callback();
}

template <typename Generator>
raw_consumer collect_values(Generator &generator, std::vector<int> &values, std::size_t &consumed, std::optional<status_code> &failure) {
    for (;;) {
        const auto result = co_await generator.next();
        if (!result) {
            failure = result.error();
            co_return;
        }

        consumed += *result;
        if (generator.done()) {
            co_return;
        }
        values.push_back(generator.value());
    }
}

struct hook_lifetime {
    std::string value;
    int        *frames_destroyed{};
};

template <typename Context> resumable<status_code> resumable_decode(Context &context, hook_lifetime &value) {
    struct frame_guard {
        int *destroyed;
        ~frame_guard() { ++*destroyed; }
    } guard{value.frames_destroyed};

    co_return co_await context(value.value);
}

} // namespace

TEST_SUITE("roundtrip/resumable_lifetimes") {

    TEST_CASE("ready resumable results do not allocate a coroutine frame") {
        bool        ready{};
        status_code status = status_code::error;
        {
            cbor::tags::test::detail::allocation_failure_guard guard;
            resumable<status_code>                             result{status_code::success};
            auto                                               awaiter = std::move(result).operator co_await();
            ready                                                      = awaiter.await_ready();
            status                                                     = awaiter.await_resume();
        }

        CHECK(ready);
        CHECK(status == status_code::success);
    }

    TEST_CASE("decoder completion resumes a raw coroutine inline") {
        const std::string      expected = "raw consumer";
        std::vector<std::byte> bytes;
        REQUIRE(make_encoder(bytes)(expected));
        input_chunks                    storage;
        auto                            source = make_input_source(storage);
        std::string                     actual;
        auto                            operation = make_resumable_decoder(source)(actual);
        bool                            resumed{};
        std::optional<operation_result> result;
        auto                            consumer = await_result(operation, result, [&] { resumed = true; });

        REQUIRE_FALSE(consumer.done());
        operation(std::span<const std::byte>{bytes}.first(1));
        CHECK_FALSE(resumed);
        operation(std::span<const std::byte>{bytes}.subspan(1));

        CHECK(resumed);
        REQUIRE(consumer.done());
        CHECK_NOTHROW(consumer.rethrow());
        REQUIRE(result);
        REQUIRE(*result);
        CHECK(**result == bytes.size());
        CHECK(actual == expected);
    }

    TEST_CASE("decoder result consumer can destroy or move the completed operation inline") {
        const std::string      expected = "consumer owns the operation";
        std::vector<std::byte> bytes;
        REQUIRE(make_encoder(bytes)(expected));
        input_chunks          storage;
        auto                  source = make_input_source(storage);
        std::string           actual;
        auto                  operation = std::optional{make_resumable_decoder(source)(actual)};
        decltype(operation)   moved;
        std::function<void()> on_complete;

        SUBCASE("destroy") {
            on_complete = [&] { operation.reset(); };
        }
        SUBCASE("move") {
            on_complete = [&] {
                moved.emplace(std::move(*operation));
                operation.reset();
            };
        }

        std::optional<operation_result> result;
        auto                            consumer = await_result(*operation, result, std::move(on_complete));
        REQUIRE_FALSE(consumer.done());
        (*operation)(bytes);

        REQUIRE(consumer.done());
        CHECK_NOTHROW(consumer.rethrow());
        REQUIRE(result);
        REQUIRE(*result);
        CHECK(**result == bytes.size());
        CHECK(actual == expected);
        CHECK_FALSE(operation);
        if (moved) {
            REQUIRE(moved->done());
            REQUIRE(moved->result());
            CHECK(*moved->result() == bytes.size());
        }
    }

    TEST_CASE("encoder result consumer can destroy the completed operation inline") {
        const std::string      expected = "output completes inline";
        std::vector<std::byte> reference;
        REQUIRE(make_encoder(reference)(expected));
        std::vector<std::byte>          bytes(reference.size());
        output_chunks                   storage;
        auto                            sink      = make_output_source(storage);
        auto                            operation = std::optional{make_resumable_encoder(sink)(expected)};
        std::optional<operation_result> result;
        auto                            consumer = await_result(*operation, result, [&] { operation.reset(); });

        REQUIRE_FALSE(consumer.done());
        (*operation)(bytes);

        REQUIRE(consumer.done());
        CHECK_NOTHROW(consumer.rethrow());
        REQUIRE(result);
        REQUIRE(*result);
        CHECK(**result == bytes.size());
        CHECK_FALSE(operation);
        std::string actual;
        REQUIRE(make_decoder(bytes)(actual));
        CHECK(actual == expected);
    }

    TEST_CASE("destroying a suspended consumer detaches its borrowed continuation") {
        const int              expected = 123456;
        std::vector<std::byte> bytes;
        REQUIRE(make_encoder(bytes)(expected));
        input_chunks                    storage;
        auto                            source = make_input_source(storage);
        int                             actual{};
        auto                            operation = make_resumable_decoder(source)(actual);
        std::optional<operation_result> abandoned_result;

        {
            auto abandoned = await_result(operation, abandoned_result, [] {});
            REQUIRE_FALSE(abandoned.done());
        }

        std::optional<operation_result> result;
        auto                            replacement = await_result(operation, result, [] {});
        REQUIRE_FALSE(replacement.done());
        operation(bytes);

        REQUIRE(replacement.done());
        CHECK_NOTHROW(replacement.rethrow());
        CHECK_FALSE(abandoned_result);
        REQUIRE(result);
        REQUIRE(*result);
        CHECK(actual == expected);
    }

    TEST_CASE("moving a pending operation retains its suspended consumer and decode state") {
        const std::string      expected = "move preserves progress";
        std::vector<std::byte> bytes;
        REQUIRE(make_encoder(bytes)(expected));
        input_chunks storage;
        auto         source = make_input_source(storage);
        std::string  actual;
        auto         original = std::optional{make_resumable_decoder(source)(actual)};
        original->operator()(std::span<const std::byte>{bytes}.first(3));
        std::optional<operation_result> result;
        auto                            consumer = await_result(*original, result, [] {});
        REQUIRE_FALSE(consumer.done());

        auto moved = std::move(*original);
        original.reset();
        moved(std::span<const std::byte>{bytes}.subspan(3));

        REQUIRE(consumer.done());
        CHECK_NOTHROW(consumer.rethrow());
        REQUIRE(result);
        REQUIRE(*result);
        CHECK(**result == bytes.size());
        CHECK(actual == expected);
    }

    TEST_CASE("cancellation destroys suspended hook frames and completes the result waiter") {
        const std::string      expected = "suspended hook payload";
        std::vector<std::byte> bytes;
        REQUIRE(make_encoder(bytes)(expected));
        input_chunks  storage;
        auto          source = make_input_source(storage);
        int           frames_destroyed{};
        hook_lifetime actual{{}, &frames_destroyed};
        auto          operation = make_resumable_decoder(source)(actual);
        operation(std::span<const std::byte>{bytes}.first(2));
        REQUIRE_FALSE(operation.done());
        CHECK(frames_destroyed == 0);

        std::optional<operation_result> result;
        auto                            consumer = await_result(operation, result, [] {});
        operation.cancel();

        REQUIRE(consumer.done());
        CHECK_NOTHROW(consumer.rethrow());
        REQUIRE(result);
        REQUIRE_FALSE(*result);
        CHECK(result->error() == status_code::cancelled);
        CHECK(frames_destroyed == 1);
        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::cancelled);
    }

    TEST_CASE("destroying a suspended root operation releases nested hook frames") {
        input_chunks  storage;
        auto          source = make_input_source(storage);
        int           frames_destroyed{};
        hook_lifetime actual{{}, &frames_destroyed};

        {
            auto operation = make_resumable_decoder(source)(actual);
            REQUIRE_FALSE(operation.done());
            CHECK(frames_destroyed == 0);
        }

        CHECK(frames_destroyed == 1);
    }

    TEST_CASE("generator consumer awaits each item and drains a whole batch inline") {
        const std::vector<int> expected{1, 24, -400, 65536};
        std::vector<std::byte> bytes;
        auto                   encoder = make_encoder(bytes);
        for (const auto value : expected) {
            REQUIRE(encoder(value));
        }

        input_chunks               storage;
        auto                       source    = make_input_source(storage);
        auto                       generator = make_resumable_generator<int>(source);
        std::vector<int>           actual;
        std::size_t                consumed{};
        std::optional<status_code> failure;
        auto                       consumer = collect_values(generator, actual, consumed, failure);
        REQUIRE_FALSE(consumer.done());

        SUBCASE("whole batch") { generator(bytes, end_of_input); }
        SUBCASE("one byte at a time") {
            for (std::size_t index = 0; index < bytes.size(); ++index) {
                generator(std::span<const std::byte>{bytes}.subspan(index, 1));
            }
            CHECK_FALSE(consumer.done());
            generator(end_of_input);
        }

        REQUIRE(consumer.done());
        CHECK_NOTHROW(consumer.rethrow());
        CHECK_FALSE(failure);
        CHECK(generator.done());
        CHECK(actual == expected);
        CHECK(consumed == bytes.size());
    }
}
