#pragma once

#include <coroutine>
#include <exception>

namespace example {

// Minimal eager consumer: the producer resumes it inline through the CBOR awaiter.
// Declare it after its awaited operation so its frame is always destroyed first.
class consumer {
  public:
    struct promise_type {
        bool success{};
        std::exception_ptr failure;

        consumer get_return_object() noexcept {
            return consumer{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() const noexcept {
            return {};
        }
        std::suspend_always final_suspend() const noexcept {
            return {};
        }
        void return_value(bool result) noexcept {
            success = result;
        }
        void unhandled_exception() noexcept {
            failure = std::current_exception();
        }
    };

    explicit consumer(std::coroutine_handle<promise_type> handle) noexcept : handle_(handle) {
    }
    consumer(const consumer &) = delete;
    consumer &operator=(const consumer &) = delete;
    ~consumer() {
        handle_.destroy();
    }

    bool done() const noexcept {
        return handle_.done();
    }
    bool succeeded() const noexcept {
        return done() && handle_.promise().success && !handle_.promise().failure;
    }

  private:
    std::coroutine_handle<promise_type> handle_;
};

} // namespace example
