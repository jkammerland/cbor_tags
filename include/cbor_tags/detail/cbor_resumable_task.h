#pragma once

#include "cbor_tags/cbor.h"
#include "cbor_tags/detail/cbor_decode_error.h"

#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>

namespace cbor::tags {

// A child task transfers directly to its parent; no scheduler is involved.
template <typename T> class resumable_task {
  public:
    struct promise_type {
        std::optional<T>        value;
        std::exception_ptr      exception;
        std::coroutine_handle<> continuation{};

        [[nodiscard]] resumable_task get_return_object() noexcept {
            return resumable_task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        [[nodiscard]] std::suspend_always initial_suspend() const noexcept { return {}; }
        struct final_awaiter {
            [[nodiscard]] bool                    await_ready() const noexcept { return false; }
            [[nodiscard]] std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> handle) const noexcept {
                return handle.promise().continuation ? handle.promise().continuation : std::noop_coroutine();
            }
            void await_resume() const noexcept {}
        };
        [[nodiscard]] final_awaiter final_suspend() const noexcept { return {}; }
        template <typename U> void  return_value(U &&result) { value.emplace(std::forward<U>(result)); }
        void                        unhandled_exception() noexcept { exception = std::current_exception(); }
    };

    using handle_type = std::coroutine_handle<promise_type>;

  private:
    handle_type handle_{};

    explicit resumable_task(handle_type handle) noexcept : handle_(handle) {}

  public:
    resumable_task()                                  = default;
    resumable_task(const resumable_task &)            = delete;
    resumable_task &operator=(const resumable_task &) = delete;
    resumable_task(resumable_task &&other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    resumable_task &operator=(resumable_task &&other) noexcept {
        if (this != &other) {
            if (handle_)
                handle_.destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    ~resumable_task() {
        if (handle_)
            handle_.destroy();
    }

    [[nodiscard]] bool        valid() const noexcept { return static_cast<bool>(handle_); }
    [[nodiscard]] bool        done() const noexcept { return handle_ && handle_.done(); }
    [[nodiscard]] handle_type handle() const noexcept { return handle_; }
    [[nodiscard]] T           result() {
        if (!handle_)
            throw std::logic_error{"resumable task has no frame"};
        auto &promise = handle_.promise();
        if (promise.exception)
            std::rethrow_exception(promise.exception);
        if (!promise.value.has_value())
            throw std::logic_error{"resumable task has no result"};
        return std::move(promise.value.value());
    }

    struct awaiter {
        handle_type handle;
        awaiter(const awaiter &) = delete;
        awaiter(awaiter &&other) noexcept : handle(std::exchange(other.handle, {})) {}
        explicit awaiter(handle_type task) noexcept : handle(task) {}
        ~awaiter() {
            if (handle)
                handle.destroy();
        }
        [[nodiscard]] bool                    await_ready() const noexcept { return !handle || handle.done(); }
        [[nodiscard]] std::coroutine_handle<> await_suspend(std::coroutine_handle<> parent) const noexcept {
            handle.promise().continuation = parent;
            return handle;
        }
        [[nodiscard]] T await_resume() {
            if (!handle)
                throw std::logic_error{"resumable task has no frame"};
            auto &promise = handle.promise();
            if (promise.exception)
                std::rethrow_exception(promise.exception);
            if (!promise.value.has_value())
                throw std::logic_error{"resumable task has no result"};
            return std::move(promise.value.value());
        }
    };

    [[nodiscard]] awaiter operator co_await() && noexcept { return awaiter{std::exchange(handle_, {})}; }
};

} // namespace cbor::tags
