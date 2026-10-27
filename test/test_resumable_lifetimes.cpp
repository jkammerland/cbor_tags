#include <algorithm>
#include <array>
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <coroutine>
#include <cstddef>
#include <doctest/doctest.h>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace cbor::tags;

namespace {

// Keep suspended consumers alive without introducing a coroutine framework.
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
        if (handle_)
            handle_.destroy();
    }

    bool done() const { return handle_.done(); }
    void rethrow() const {
        if (handle_.promise().failure)
            std::rethrow_exception(handle_.promise().failure);
    }

  private:
    std::coroutine_handle<promise_type> handle_;
};

using operation_result = expected<std::size_t, status_code>;

template <typename Operation, typename Callback>
raw_consumer await_result(Operation &operation, std::optional<operation_result> &result, Callback callback) {
    result.emplace(co_await operation);
    callback();
}

template <typename Generator> raw_consumer collect_values(Generator &generator, std::vector<int> &values) {
    while (co_await generator.next())
        values.push_back(generator.value());
}

template <typename Generator> raw_consumer collect_prefixes(Generator &generator, std::vector<std::byte> &bytes, std::size_t &prefixes) {
    while (co_await generator.next()) {
        ++prefixes;
        for (auto byte : generator.value())
            bytes.push_back(byte);
    }
}

void append_produced(const resumable_feed_result &feed, std::vector<std::byte> &bytes) {
    for (const auto &prefix : feed.produced()) {
        for (auto byte : prefix)
            bytes.push_back(byte);
    }
}

struct callback_value {
    int                   payload{};
    std::function<void()> after_decode;
    bool                  hook_returned{};
};

template <typename Context> typename Context::task_type resumable_decode(Context &context, callback_value &value) {
    const auto status = co_await context(value.payload);
    if (status != status_code::success)
        co_return status;

    // Cancellation must leave the currently executing hook frame alive.
    value.after_decode();
    value.hook_returned = true;
    co_return status_code::success;
}

struct encode_callback_value {
    int                   payload{};
    std::function<void()> after_encode;
};

template <typename Context> typename Context::task_type resumable_encode(Context &context, const encode_callback_value &value) {
    const auto status = co_await context(value.payload);
    if (status != status_code::success)
        co_return status;

    value.after_encode();
    co_return status_code::success;
}

} // namespace

TEST_SUITE("roundtrip/resumable_lifetimes") {
    TEST_CASE("operation replacement can destroy either owner during cancellation delivery") {
        int  first{};
        int  second{};
        auto factory         = make_resumable_decoder();
        using operation_type = decltype(factory(first));
        auto operation       = std::make_unique<operation_type>(factory(first));
        auto replacement     = std::make_unique<operation_type>(factory(second));
        bool destroy_destination{};
        SUBCASE("destination owner") { destroy_destination = true; }
        SUBCASE("source owner") { destroy_destination = false; }

        std::optional<operation_result> result;
        auto                            consumer = await_result(*operation, result, [&] {
            // The replacement must be visible before the old waiter is notified.
            CHECK_FALSE(operation->done());
            if (destroy_destination)
                operation.reset();
            else
                replacement.reset();
        });
        REQUIRE_FALSE(consumer.done());
        *operation = std::move(*replacement);

        REQUIRE(consumer.done());
        CHECK_NOTHROW(consumer.rethrow());
        REQUIRE(result);
        REQUIRE_FALSE(*result);
        CHECK(result->error() == status_code::cancelled);
        CHECK(static_cast<bool>(operation) != destroy_destination);

        if (operation) {
            std::vector<std::byte> encoded;
            REQUIRE(make_encoder(encoded)(1234));
            (void)(*operation)(encoded);
            REQUIRE(operation->result());
            CHECK_EQ(second, 1234);
        }
    }

    TEST_CASE("generator replacement installs its value slot before notifying the old waiter") {
        using generator_type = decltype(make_resumable_decoder().items<int>());
        auto generator       = std::make_unique<generator_type>(make_resumable_decoder().items<int>());
        auto replacement     = std::make_unique<generator_type>(make_resumable_decoder().elements<int>());
        bool destroy_destination{};
        SUBCASE("destination owner") { destroy_destination = true; }
        SUBCASE("source owner") { destroy_destination = false; }

        std::optional<operation_result> result;
        auto                            consumer = await_result(*generator, result, [&] {
            // Reading the new generator here exercises the derived value_ slot.
            const std::vector<int> source{17, 300};
            std::vector<std::byte> encoded;
            REQUIRE(make_encoder(encoded)(source));
            (void)(*generator)(encoded);
            std::vector<int> decoded;
            auto             reader = collect_values(*generator, decoded);
            REQUIRE(reader.done());
            CHECK_NOTHROW(reader.rethrow());
            CHECK(decoded == source);
            REQUIRE(generator->result());
            if (destroy_destination)
                generator.reset();
            else
                replacement.reset();
        });
        REQUIRE_FALSE(consumer.done());
        *generator = std::move(*replacement);

        REQUIRE(consumer.done());
        CHECK_NOTHROW(consumer.rethrow());
        REQUIRE(result);
        REQUIRE_FALSE(*result);
        CHECK(result->error() == status_code::cancelled);
        CHECK(static_cast<bool>(generator) != destroy_destination);
    }

    TEST_CASE("decoder result waiter can destroy or move its operation inline") {
        const std::string      source = "consumer owns the operation";
        std::vector<std::byte> encoded;
        REQUIRE(make_encoder(encoded)(source));

        std::string           decoded;
        auto                  operation = std::optional{make_resumable_decoder()(decoded)};
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
        const auto feed = (*operation)(encoded);

        REQUIRE(consumer.done());
        CHECK_NOTHROW(consumer.rethrow());
        REQUIRE(result.has_value());
        REQUIRE(*result);
        CHECK(**result == encoded.size());
        CHECK(decoded == source);
        CHECK_FALSE(operation.has_value());
        CHECK(feed.state == resumable_state::done);
        CHECK(feed.bytes == encoded.size());
        if (moved) {
            CHECK(moved->done());
            REQUIRE(moved->result());
            CHECK(*moved->result() == encoded.size());
        }
    }

    TEST_CASE("encoder result waiter can destroy or move its operation inline") {
        const std::string      source = "output completes inline";
        std::vector<std::byte> reference;
        REQUIRE(make_encoder(reference)(source));

        auto                  operation = std::optional{make_resumable_encoder()(source)};
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
        std::vector<std::byte> output(reference.size());
        const auto             feed = (*operation)(output);

        REQUIRE(consumer.done());
        CHECK_NOTHROW(consumer.rethrow());
        REQUIRE(result.has_value());
        REQUIRE(*result);
        CHECK(**result == output.size());
        CHECK_FALSE(operation.has_value());
        CHECK(feed.state == resumable_state::done);
        CHECK(feed.bytes == output.size());
        std::string decoded;
        REQUIRE(make_decoder(output)(decoded));
        CHECK(decoded == source);
        if (moved)
            CHECK(moved->done());
    }

    TEST_CASE("destroying a suspended result waiter detaches it before later input") {
        const int              source = 123456;
        std::vector<std::byte> encoded;
        REQUIRE(make_encoder(encoded)(source));

        int                             decoded{};
        auto                            operation = make_resumable_decoder()(decoded);
        std::optional<operation_result> abandoned_result;
        {
            auto abandoned = await_result(operation, abandoned_result, [] {});
            REQUIRE_FALSE(abandoned.done());
        }

        std::optional<operation_result> result;
        auto                            replacement = await_result(operation, result, [] {});
        REQUIRE_FALSE(replacement.done());
        const auto feed = operation(encoded);

        REQUIRE(replacement.done());
        CHECK_NOTHROW(replacement.rethrow());
        CHECK_FALSE(abandoned_result.has_value());
        REQUIRE(result.has_value());
        REQUIRE(*result);
        CHECK(decoded == source);
        CHECK(feed.state == resumable_state::done);
    }

    TEST_CASE("destroying a suspended next waiter leaves the first item available") {
        const int              source = 127;
        std::vector<std::byte> encoded;
        REQUIRE(make_encoder(encoded)(source));

        auto             generator = make_resumable_decoder().items<int>();
        std::vector<int> abandoned_values;
        {
            auto abandoned = collect_values(generator, abandoned_values);
            REQUIRE_FALSE(abandoned.done());
        }
        const auto feed = generator(encoded, end_of_input);
        REQUIRE(feed.state == resumable_state::yielded);

        std::vector<int> values;
        auto             replacement = collect_values(generator, values);
        REQUIRE(replacement.done());
        CHECK_NOTHROW(replacement.rethrow());
        CHECK(abandoned_values.empty());
        CHECK(values == std::vector<int>{source});
        REQUIRE(generator.result());
        CHECK(*generator.result() == encoded.size());
    }

    TEST_CASE("prefed item and element generators expose each value exactly once") {
        const std::array<int, 4> source{7, 42, 300, -12};
        std::vector<std::byte>   encoded;
        auto                     decoder   = make_resumable_decoder();
        auto                     generator = decoder.items<int>();
        SUBCASE("items") { REQUIRE(make_encoder(encoded)(source[0], source[1], source[2], source[3])); }
        SUBCASE("array elements") {
            REQUIRE(make_encoder(encoded)(source));
            generator = decoder.elements<int>();
        }

        const auto feed = generator(encoded, end_of_input);
        REQUIRE(feed.state == resumable_state::yielded);
        std::vector<int> values;
        auto             consumer = collect_values(generator, values);

        REQUIRE(consumer.done());
        CHECK_NOTHROW(consumer.rethrow());
        CHECK(std::ranges::equal(values, source));
        CHECK(generator.done());
        REQUIRE(generator.result());
        CHECK(*generator.result() == encoded.size());
    }

    TEST_CASE("prefed output generator exposes its first prefix exactly once") {
        const std::string      source = "a prefix already exists before next";
        std::vector<std::byte> reference;
        REQUIRE(make_encoder(reference)(source));

        auto                   generator = make_resumable_encoder().chunks(source);
        std::vector<std::byte> output(reference.size());
        const auto             feed = generator(output);
        REQUIRE(feed.state == resumable_state::yielded);

        std::vector<std::byte> collected;
        std::size_t            prefixes{};
        auto                   consumer = collect_prefixes(generator, collected, prefixes);
        REQUIRE(consumer.done());
        CHECK_NOTHROW(consumer.rethrow());
        CHECK(prefixes == 1U);
        CHECK(collected.size() == reference.size());
        std::string decoded;
        REQUIRE(make_decoder(collected)(decoded));
        CHECK(decoded == source);
        REQUIRE(generator.result());
        CHECK(*generator.result() == collected.size());
    }

    TEST_CASE("input feed reports all items drained by an inline consumer") {
        const std::array<int, 4> source{9, -400, 65536, 25};
        std::vector<std::byte>   encoded;
        auto                     decoder   = make_resumable_decoder();
        auto                     generator = decoder.items<int>();
        SUBCASE("items") { REQUIRE(make_encoder(encoded)(source[0], source[1], source[2], source[3])); }
        SUBCASE("array elements") {
            REQUIRE(make_encoder(encoded)(source));
            generator = decoder.elements<int>();
        }

        std::vector<int> values;
        auto             consumer = collect_values(generator, values);
        REQUIRE_FALSE(consumer.done());
        const auto feed = generator(encoded, end_of_input);

        REQUIRE(consumer.done());
        CHECK_NOTHROW(consumer.rethrow());
        CHECK(std::ranges::equal(values, source));
        CHECK(feed.bytes == encoded.size());
        CHECK(feed.state == resumable_state::done);
        CHECK(feed.error == status_code::success);
        CHECK(generator.done());
    }

    TEST_CASE("output feed reports all prefixes drained by an inline consumer") {
        const std::string      source = "consumer drains several small output buffers";
        std::vector<std::byte> reference;
        REQUIRE(make_encoder(reference)(source));

        auto                   generator = make_resumable_encoder().chunks(source);
        std::vector<std::byte> collected;
        std::size_t            prefixes{};
        auto                   consumer = collect_prefixes(generator, collected, prefixes);
        REQUIRE_FALSE(consumer.done());
        std::vector<std::array<std::byte, 3>> buffers((reference.size() + 2U) / 3U);
        const auto                            feed = generator(buffers);

        REQUIRE(consumer.done());
        CHECK_NOTHROW(consumer.rethrow());
        CHECK(prefixes == buffers.size());
        CHECK(feed.accepted_chunks == buffers.size());
        CHECK(feed.bytes == reference.size());
        CHECK(feed.state == resumable_state::done);
        CHECK(feed.error == status_code::success);
        std::string decoded;
        REQUIRE(make_decoder(collected)(decoded));
        CHECK(decoded == source);
    }

    TEST_CASE("reused output descriptors produce only the bytes of their new operation") {
        const std::string      first  = "first";
        const std::string      second = "a separate value";
        std::vector<std::byte> reference;
        REQUIRE(make_encoder(reference)(first, second));

        std::vector<std::byte> output(reference.size());
        auto                   encoder         = make_resumable_encoder();
        auto                   first_operation = encoder(first);
        const auto             first_feed      = first_operation(output);
        REQUIRE(first_feed.state == resumable_state::done);
        REQUIRE_FALSE(first_feed.unused_output().empty());

        auto       second_operation = encoder(second);
        const auto second_feed      = second_operation(first_feed.unused_output());
        REQUIRE(second_feed.state == resumable_state::done);
        std::vector<std::byte> produced;
        append_produced(second_feed, produced);
        CHECK(produced.size() == second_feed.bytes);
        CHECK(first_feed.bytes + produced.size() == output.size());

        std::string decoded_second;
        REQUIRE(make_decoder(produced)(decoded_second));
        CHECK(decoded_second == second);
        std::string decoded_first;
        decoded_second.clear();
        REQUIRE(make_decoder(output)(decoded_first, decoded_second));
        CHECK(decoded_first == first);
        CHECK(decoded_second == second);
    }

    TEST_CASE("plain encoder releases caller output before suspending for more space") {
        const std::string      source = "a later buffer supplies the remaining space";
        std::vector<std::byte> reference;
        REQUIRE(make_encoder(reference)(source));

        auto                     operation = make_resumable_encoder()(source);
        std::array<std::byte, 1> first_buffer{};
        const auto               first_feed = operation(first_buffer);
        REQUIRE(first_feed.state == resumable_state::need_output);
        std::vector<std::byte> collected;
        append_produced(first_feed, collected);

        // Reusing the caller's buffer after return must not expose it to later writes.
        first_buffer.fill(std::byte{0x55});
        const auto                      reused = first_buffer;
        std::optional<operation_result> result;
        auto                            consumer = await_result(operation, result, [] {});
        REQUIRE_FALSE(consumer.done());
        CHECK(first_buffer == reused);

        std::vector<std::byte> remaining(reference.size() - collected.size());
        const auto             second_feed = operation(remaining);
        REQUIRE(consumer.done());
        CHECK_NOTHROW(consumer.rethrow());
        CHECK(first_buffer == reused);
        CHECK(second_feed.state == resumable_state::done);
        append_produced(second_feed, collected);
        std::string decoded;
        REQUIRE(make_decoder(collected)(decoded));
        CHECK(decoded == source);
        REQUIRE(result.has_value());
        REQUIRE(*result);
        CHECK(**result == collected.size());
    }

    TEST_CASE("cancelling or destroying an operation inside its hook defers waiter resumption") {
        const int              source = 91234;
        std::vector<std::byte> encoded;
        REQUIRE(make_encoder(encoded)(source));

        callback_value decoded;
        auto           operation = std::optional{make_resumable_decoder()(decoded)};
        SUBCASE("cancel") {
            decoded.after_decode = [&] { operation->cancel(); };
        }
        SUBCASE("destroy") {
            decoded.after_decode = [&] { operation.reset(); };
        }

        bool                            observed_hook_return{};
        std::optional<operation_result> result;
        auto                            consumer = await_result(*operation, result, [&] { observed_hook_return = decoded.hook_returned; });
        REQUIRE_FALSE(consumer.done());
        const auto feed = (*operation)(encoded);

        REQUIRE(consumer.done());
        CHECK_NOTHROW(consumer.rethrow());
        CHECK(decoded.payload == source);
        CHECK(decoded.hook_returned);
        CHECK(observed_hook_return);
        REQUIRE(result.has_value());
        REQUIRE_FALSE(*result);
        CHECK(result->error() == status_code::cancelled);
        CHECK(feed.state == resumable_state::failed);
        CHECK(feed.error == status_code::cancelled);
    }

    TEST_CASE("decoder cancellation leaves later buffered values unread") {
        const int              first  = 731;
        const int              second = -3827;
        std::vector<std::byte> encoded;
        std::vector<std::byte> first_encoded;
        REQUIRE(make_encoder(encoded)(first, second));
        REQUIRE(make_encoder(first_encoded)(first));

        callback_value decoded;
        int            later_destination = 99;
        auto           operation         = make_resumable_decoder()(decoded, later_destination);
        decoded.after_decode             = [&] { operation.cancel(); };
        const auto feed                  = operation(encoded);

        CHECK(feed.state == resumable_state::failed);
        CHECK(feed.error == status_code::cancelled);
        CHECK(feed.bytes == first_encoded.size());
        CHECK(decoded.payload == first);
        CHECK(decoded.hook_returned);
        CHECK(later_destination == 99);
        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::cancelled);

        // The unrequested suffix remains usable by a separate decoder.
        std::vector<std::byte> remaining;
        for (const auto &chunk : operation.remaining_input()) {
            for (auto byte : chunk)
                remaining.push_back(byte);
        }
        CHECK(remaining.size() + first_encoded.size() == encoded.size());
        int remaining_value{};
        REQUIRE(make_decoder(remaining)(remaining_value));
        CHECK(remaining_value == second);
    }

    TEST_CASE("encoder cancellation leaves later output space unwritten") {
        encode_callback_value  first{731, {}};
        const int              second = -3827;
        std::vector<std::byte> reference;
        std::vector<std::byte> first_encoded;
        REQUIRE(make_encoder(reference)(first.payload, second));
        REQUIRE(make_encoder(first_encoded)(first.payload));

        auto operation     = make_resumable_encoder()(first, second);
        first.after_encode = [&] { operation.cancel(); };
        std::vector<std::byte> output(reference.size(), std::byte{0x55});
        const auto             feed = operation(output);

        CHECK(feed.state == resumable_state::failed);
        CHECK(feed.error == status_code::cancelled);
        CHECK(feed.bytes == first_encoded.size());
        REQUIRE(operation.done());
        REQUIRE_FALSE(operation.result());
        CHECK(operation.result().error() == status_code::cancelled);
        const auto unused = std::span{output}.subspan(first_encoded.size());
        CHECK(std::ranges::all_of(unused, [](std::byte byte) { return byte == std::byte{0x55}; }));

        std::vector<std::byte> produced;
        append_produced(feed, produced);
        CHECK(produced.size() == first_encoded.size());
        int decoded{};
        REQUIRE(make_decoder(produced)(decoded));
        CHECK(decoded == first.payload);
    }
}
