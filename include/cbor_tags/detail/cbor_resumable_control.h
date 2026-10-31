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
template <typename T> class [[nodiscard]] resumable {
  public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        std::optional<T>        value;
        std::exception_ptr      error;
        std::coroutine_handle<> continuation{std::noop_coroutine()};

        resumable           get_return_object() noexcept { return resumable{handle_type::from_promise(*this)}; }
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

    resumable(T value) : ready_(std::move(value)) {}
    resumable(resumable &&other) noexcept : ready_(std::move(other.ready_)), handle_(std::exchange(other.handle_, {})) {}
    resumable &operator=(resumable &&other) noexcept {
        if (this != &other) {
            if (handle_)
                handle_.destroy();
            ready_  = std::move(other.ready_);
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    resumable(const resumable &) = delete;
    ~resumable() {
        if (handle_)
            handle_.destroy();
    }

    bool        done() const noexcept { return ready_.has_value() || (handle_ && handle_.done()); }
    handle_type handle() const noexcept { return handle_; }
    T           result() {
        if (!done())
            throw std::logic_error("resumable result is not ready");
        if (ready_)
            return std::move(*ready_);
        auto &promise = handle_.promise();
        if (promise.error)
            std::rethrow_exception(promise.error);
        assert(promise.value.has_value());
        return std::move(*promise.value);
    }
    struct awaiter {
        resumable               value;
        bool                    await_ready() const noexcept { return value.done(); }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> parent) const noexcept {
            value.handle_.promise().continuation = parent;
            return value.handle_;
        }
        T await_resume() { return value.result(); }
    };
    awaiter operator co_await() && noexcept { return {std::move(*this)}; }

  private:
    explicit resumable(handle_type handle) noexcept : handle_(handle) {}
    std::optional<T> ready_;
    handle_type      handle_{};
};

enum class resume_state { need_input, need_output, done };
struct end_of_input_t {};
inline constexpr end_of_input_t end_of_input{};
struct until_end_of_input_t {};
inline constexpr until_end_of_input_t until_end_of_input{};

namespace detail {
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

template <typename Context> class resume_operation {
    struct state {
        resume_control                     control;
        Context                            context;
        resumable<status_code>             work{status_code::success};
        expected<std::size_t, status_code> result{unexpected<status_code>{status_code::incomplete}};
        std::size_t                        start{};
        bool                               finished{};

        template <typename... Args>
        state(typename Context::source_type &source, Args &&...args) : context(source, control), start(context.offset()) {
            work         = context(std::forward<Args>(args)...);
            control.leaf = work.handle();
        }
    };
    std::unique_ptr<state> state_;

    // Publish completion before waking user code. The waiter may destroy this operation.
    static resume_state drive(state &s) {
        if (s.finished || s.control.driving)
            return s.finished ? resume_state::done : Context::waiting_state;
        s.control.driving = true;
        if (auto handle = std::exchange(s.control.leaf, {}))
            handle.resume();
        if (s.work.done()) {
            status_code status;
            try {
                status = s.work.result();
            } catch (...) { status = resumable_exception_status(); }
            s.result   = status == status_code::success ? expected<std::size_t, status_code>{s.context.offset() - s.start}
                                                        : expected<std::size_t, status_code>{unexpected<status_code>{status}};
            s.finished = true;
        }
        s.control.driving   = false;
        auto       waiter   = s.finished ? std::exchange(s.control.waiter, {}) : std::coroutine_handle<>{};
        const auto progress = s.finished ? resume_state::done : Context::waiting_state;
        if (waiter)
            waiter.resume();
        return progress;
    }

  public:
    template <typename... Args>
    resume_operation(typename Context::source_type &source, Args &&...args)
        : state_(std::make_unique<state>(source, std::forward<Args>(args)...)) {
        source.acquire();
        drive(*state_);
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

    template <typename... Chunks> resume_state operator()(Chunks &&...chunks) {
        auto &s = *state_;
        if (s.finished)
            return resume_state::done;
        bool changed{};
        bool close{};
        auto append = [&](auto &&chunk) {
            if constexpr (std::same_as<std::remove_cvref_t<decltype(chunk)>, end_of_input_t>) {
                close = true;
            } else {
                changed = s.context.source().append(std::forward<decltype(chunk)>(chunk)) != 0 || changed;
            }
        };
        (append(std::forward<Chunks>(chunks)), ...);
        if (close) {
            s.context.source().close();
            changed = true;
        }
        return changed ? drive(s) : Context::waiting_state;
    }
    resume_state finish() {
        auto &s = *state_;
        s.context.source().close();
        return drive(s);
    }
    void cancel() {
        auto &s = *state_;
        if (s.finished)
            return;
        if (s.control.driving)
            throw std::logic_error("cannot cancel a running codec");
        s.work = resumable<status_code>{status_code::cancelled};
        s.context.rollback();
        s.control.leaf = {};
        s.result       = unexpected<status_code>{status_code::cancelled};
        s.finished     = true;
        auto waiter    = std::exchange(s.control.waiter, {});
        if (waiter)
            waiter.resume();
    }
    bool                               done() const noexcept { return state_ && state_->finished; }
    resume_state                       progress() const noexcept { return done() ? resume_state::done : Context::waiting_state; }
    expected<std::size_t, status_code> result() const { return state_->result; }
    auto                               remaining_input() const
        requires(Context::waiting_state == resume_state::need_input)
    {
        return state_->context.remaining_input();
    }

    auto produced() const
        requires(Context::waiting_state == resume_state::need_output)
    {
        return state_->context.produced();
    }
    auto remaining_output() const
        requires(Context::waiting_state == resume_state::need_output)
    {
        return state_->context.remaining_output();
    }

    struct awaiter {
        state                  *owner;
        std::coroutine_handle<> registered{};
        bool                    await_ready() const noexcept { return owner->finished; }
        void                    await_suspend(std::coroutine_handle<> handle) {
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
    void release() noexcept {
        if (state_) {
            state_->context.source().release();
            state_.reset();
        }
    }
};
} // namespace detail
} // namespace cbor::tags
