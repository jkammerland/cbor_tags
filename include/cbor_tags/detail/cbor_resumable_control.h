#pragma once

#include "cbor_tags/cbor.h"

#include <cassert>
#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace cbor::tags {

// Small ordinary items return inline. Only a suspended computation owns a frame.
template <typename T> class [[nodiscard]] resumable_task {
  public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        std::optional<T>        value;
        std::exception_ptr      error;
        std::coroutine_handle<> continuation{std::noop_coroutine()};

        resumable_task      get_return_object() noexcept { return resumable_task{handle_type::from_promise(*this)}; }
        std::suspend_always initial_suspend() const noexcept { return {}; }
        struct final_awaiter {
            bool                    await_ready() const noexcept { return false; }
            std::coroutine_handle<> await_suspend(handle_type handle) const noexcept { return handle.promise().continuation; }
            void                    await_resume() const noexcept {}
        };
        final_awaiter final_suspend() const noexcept { return {}; }
        void          return_value(T result) { value.emplace(std::move(result)); }
        void          unhandled_exception() noexcept { error = std::current_exception(); }
    };

    resumable_task(T value) : ready_(std::move(value)) {}
    resumable_task(resumable_task &&other) noexcept : ready_(std::move(other.ready_)), handle_(std::exchange(other.handle_, {})) {}
    resumable_task &operator=(resumable_task &&other) noexcept {
        if (this != &other) {
            if (handle_)
                handle_.destroy();
            ready_  = std::move(other.ready_);
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    resumable_task(const resumable_task &) = delete;
    ~resumable_task() {
        if (handle_)
            handle_.destroy();
    }

    bool        done() const noexcept { return ready_.has_value() || (handle_ && handle_.done()); }
    handle_type handle() const noexcept { return handle_; }
    T           result() {
        if (!done())
            throw std::logic_error("resumable_task result is not ready");
        if (ready_)
            return std::move(*ready_);
        auto &promise = handle_.promise();
        if (promise.error)
            std::rethrow_exception(promise.error);
        assert(promise.value.has_value());
        return std::move(*promise.value);
    }
    struct awaiter {
        resumable_task          value;
        bool                    await_ready() const noexcept { return value.done(); }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> parent) const noexcept {
            value.handle_.promise().continuation = parent;
            return value.handle_;
        }
        T await_resume() { return value.result(); }
    };
    awaiter operator co_await() && noexcept { return {std::move(*this)}; }

  private:
    explicit resumable_task(handle_type handle) noexcept : handle_(handle) {}
    std::optional<T> ready_;
    handle_type      handle_{};
};

enum class resume_state { need_input, need_output, finished };
struct until_end_of_input_t {};
inline constexpr until_end_of_input_t until_end_of_input{};

namespace detail {
template <typename Context>
concept HasResumableSuspendHook = requires(Context &context) {
    { context.suspend() } -> std::same_as<void>;
};

// Any at_capacity() opts in, so a misdeclared one fails to compile instead of
// being ignored
template <typename Source>
concept NamesCapacityReport = requires(Source &source) { source.at_capacity(); };
template <typename Source>
concept HasCapacityReport = requires(const Source &source) {
    { source.at_capacity() } noexcept -> std::same_as<bool>;
};

struct resume_control {
    std::coroutine_handle<> leaf{};
    std::coroutine_handle<> waiter{};
    bool                    driving{};

    struct wait_awaiter {
        resume_control &control;
        bool            await_ready() const noexcept { return false; }
        void            await_suspend(std::coroutine_handle<> handle) const noexcept { control.leaf = handle; }
        void            await_resume() const noexcept {}
    };
    wait_awaiter wait() noexcept { return {*this}; }
};

inline status_code resumable_exception_status() noexcept {
    try {
        throw;
    } catch (const std::bad_alloc &) { return status_code::out_of_memory; } catch (const std::length_error &) {
        return status_code::out_of_memory;
    } catch (...) { return status_code::error; }
}

template <typename Context> class [[nodiscard]] resume_operation {
    struct operation_state {
        resume_control                     control;
        Context                            context;
        resumable_task<status_code>        work{status_code::success};
        expected<std::size_t, status_code> result{unexpected<status_code>{status_code::incomplete}};
        std::size_t                        start{};
        bool                               finished{};

        template <typename... Args>
        operation_state(typename Context::source_type &source, Args &&...args) : context(source, control), start(context.offset()) {
            work         = context(std::forward<Args>(args)...);
            control.leaf = work.handle();
        }
    };
    std::unique_ptr<operation_state> state_;

    // Publish completion before waking user code. The waiter may destroy this operation.
    static resume_state drive(operation_state &s) {
        if (s.finished || s.control.driving)
            return s.finished ? resume_state::finished : Context::waiting_state;
        s.control.driving = true;
        if (auto handle = std::exchange(s.control.leaf, {}))
            handle.resume();
        // A caller may relocate or reclaim storage after this drive returns.
        if constexpr (HasResumableSuspendHook<Context>)
            s.context.suspend();
        if (s.work.done()) {
            status_code status;
            try {
                status = s.work.result();
            } catch (...) { status = resumable_exception_status(); }
            s.result   = status == status_code::success ? expected<std::size_t, status_code>{s.context.offset() - s.start}
                                                        : expected<std::size_t, status_code>{unexpected<status_code>{status}};
            s.finished = true;
        } else if (stalled(s)) {
            end(s, status_code::capacity_exceeded);
        }
        s.control.driving   = false;
        auto       waiter   = s.finished ? std::exchange(s.control.waiter, {}) : std::coroutine_handle<>{};
        const auto progress = s.finished ? resume_state::finished : Context::waiting_state;
        if (waiter)
            waiter.resume();
        return progress;
    }

  public:
    template <typename... Args> resume_operation(typename Context::source_type &source, Args &&...args) {
        // Acquire before constructing a context: an ordinary codec can finish
        // eagerly while the operation state is being constructed.
        source.acquire();
        try {
            source.refresh();
            state_ = std::make_unique<operation_state>(source, std::forward<Args>(args)...);
            drive(*state_);
        } catch (...) {
            state_.reset();
            source.release();
            throw;
        }
    }
    resume_operation(resume_operation &&) noexcept = default;
    resume_operation &operator=(resume_operation &&other) noexcept {
        if (this != &other) {
            release();
            state_ = std::move(other.state_);
        }
        return *this;
    }
    resume_operation(const resume_operation &) = delete;
    ~resume_operation() { release(); }

    resume_state resume() {
        auto &s = checked_state();
        if (s.finished)
            return resume_state::finished;
        if (s.control.driving)
            throw std::logic_error("cannot resume a running codec");

        s.context.source().refresh();
        return drive(s);
    }
    resume_state finish() {
        auto &s = checked_state();
        if (s.control.driving)
            throw std::logic_error("cannot finish a running codec");
        s.context.source().close();
        return resume();
    }
    void cancel() {
        // Nothing to cancel in a moved-from operation, same as a finished one
        if (!state_)
            return;
        auto &s = *state_;
        if (s.finished)
            return;
        if (s.control.driving)
            throw std::logic_error("cannot cancel a running codec");

        // The waiter may destroy this operation during inline resumption.
        if (auto waiter = stop(s))
            waiter.resume();
    }
    bool                               done() const noexcept { return state_ && state_->finished; }
    resume_state                       state() const noexcept { return done() ? resume_state::finished : Context::waiting_state; }
    expected<std::size_t, status_code> result() const {
        // incomplete would look like truncated input, so a pending result is a caller bug
        const auto &s = checked_state();
        if (!s.finished)
            throw std::logic_error("resumable operation has not finished");
        return s.result;
    }
    std::size_t committed_bytes() const { return state_ ? state_->context.offset() - state_->start : 0; }
    auto        remaining_input() const
        requires(Context::waiting_state == resume_state::need_input)
    {
        return checked_state().context.remaining_input();
    }

    auto remaining_output() const
        requires(Context::waiting_state == resume_state::need_output)
    {
        return checked_state().context.remaining_output();
    }

    struct awaiter {
        operation_state        *owner;
        std::coroutine_handle<> registered{};
        bool                    await_ready() const noexcept { return owner && owner->finished; }
        void                    await_suspend(std::coroutine_handle<> handle) {
            if (!owner)
                throw std::logic_error("resumable operation was moved from");
            if (owner->control.waiter)
                throw std::logic_error("operation already has a waiter");
            registered            = handle;
            owner->control.waiter = handle;
        }
        expected<std::size_t, status_code> await_resume() {
            registered = {};
            return owner->result;
        }
        ~awaiter() {
            if (registered && owner->control.waiter == registered)
                owner->control.waiter = {};
        }
    };
    awaiter operator co_await() & noexcept { return {state_.get()}; }

  private:
    operation_state &checked_state() const {
        if (!state_)
            throw std::logic_error("resumable operation was moved from");
        return *state_;
    }

    // Ends a pending operation with status. Waking the waiter is up to the caller.
    static void end(operation_state &s, status_code status) noexcept {
        // Retire every suspended handle before destroying its owning frames.
        // Input retains its committed source position; the dead reader needs no reset.
        s.control.leaf = {};
        s.work         = resumable_task<status_code>{status};
        if constexpr (Context::waiting_state == resume_state::need_output)
            s.context.rollback();

        s.result   = unexpected<status_code>{status};
        s.finished = true;
    }

    // Ends a pending operation as cancelled. Returns the waiter to wake.
    static std::coroutine_handle<> stop(operation_state &s) noexcept {
        end(s, status_code::cancelled);
        return std::exchange(s.control.waiter, {});
    }

    // The source says reclaiming can't give the waiting codec more, so it would
    // wait forever
    static bool stalled(operation_state &s) noexcept {
        using source_type = typename Context::source_type;
        if constexpr (NamesCapacityReport<source_type>) {
            static_assert(HasCapacityReport<source_type>, "at_capacity() must be a const noexcept member returning bool");
            return std::as_const(s.context.source()).at_capacity();
        } else
            return false;
    }

    void release() noexcept {
        if (state_) {
            // Going away while pending is a cancel. Wake the waiter while the
            // state it reads still exists, instead of leaving it hanging on freed memory.
            if (!state_->finished && !state_->control.driving) {
                if (auto waiter = stop(*state_))
                    waiter.resume();
            }

            auto &source = state_->context.source();
            // Abandoned partial output must not become part of the next item.
            // Destroy suspended frames before restoring their retry boundary.
            state_->work = resumable_task<status_code>{status_code::cancelled};
            if constexpr (Context::waiting_state == resume_state::need_output)
                state_->context.rollback();
            state_.reset();
            source.release();
        }
    }
};
} // namespace detail
} // namespace cbor::tags
