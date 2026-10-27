#pragma once

#include "cbor_tags/detail/cbor_resumable_state.h"

#include <cassert>
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <ranges>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

namespace cbor::tags {

// Supplying this wrapper explicitly lends a writable buffer to a generator.
struct output_buffer_lease {
    writable_chunk chunk;
    explicit output_buffer_lease(writable_chunk value) : chunk(std::move(value)) {}
    output_buffer_lease(const output_buffer_lease &)                = delete;
    output_buffer_lease &operator=(const output_buffer_lease &)     = delete;
    output_buffer_lease(output_buffer_lease &&) noexcept            = default;
    output_buffer_lease &operator=(output_buffer_lease &&) noexcept = default;
};

template <typename R>
    requires(detail::resumable_writable_range<R> && detail::resumable_safe_range<R>)
[[nodiscard]] output_buffer_lease lease_output(R &&range) {
    return output_buffer_lease{writable_chunk{std::forward<R>(range)}};
}

namespace detail {

template <typename T>
concept input_chunk_arg = std::same_as<std::remove_cvref_t<T>, readable_chunk> || (resumable_readable_range<T> && resumable_safe_range<T>);

template <typename T>
concept output_chunk_arg = std::same_as<std::remove_cvref_t<T>, writable_chunk> || (resumable_writable_range<T> && resumable_safe_range<T>);

template <typename T>
concept input_batch_arg = std::ranges::input_range<T> && !resumable_readable_range<T> && requires {
    requires input_chunk_arg<std::ranges::range_reference_t<T>>;
    requires std::is_lvalue_reference_v<T> || std::ranges::borrowed_range<T> ||
                 std::ranges::borrowed_range<std::remove_cvref_t<std::ranges::range_reference_t<T>>> ||
                 std::same_as<std::remove_cvref_t<std::ranges::range_reference_t<T>>, readable_chunk>;
};

template <typename T>
concept output_batch_arg = std::ranges::input_range<T> && !resumable_readable_range<T> && requires {
    requires output_chunk_arg<std::ranges::range_reference_t<T>>;
    requires std::is_lvalue_reference_v<T> || std::ranges::borrowed_range<T> ||
                 std::ranges::borrowed_range<std::remove_cvref_t<std::ranges::range_reference_t<T>>> ||
                 std::same_as<std::remove_cvref_t<std::ranges::range_reference_t<T>>, writable_chunk>;
};

template <typename T>
concept leased_output_arg = std::same_as<std::remove_cvref_t<T>, output_buffer_lease> && !std::is_lvalue_reference_v<T>;

template <typename T>
concept leased_output_batch = std::ranges::input_range<T> && requires {
    requires std::same_as<std::remove_cvref_t<std::ranges::range_reference_t<T>>, output_buffer_lease>;
};

template <typename T>
concept input_feed_arg = std::same_as<std::remove_cvref_t<T>, end_of_input_t> || input_chunk_arg<T> || input_batch_arg<T>;

template <typename T>
concept output_feed_arg = output_chunk_arg<T> || output_batch_arg<T>;

inline void classify_exception(resumable_io &io) noexcept {
    try {
        throw;
    } catch (const std::bad_alloc &) { io.fail(status_code::out_of_memory); } catch (const std::length_error &) {
        io.fail(status_code::out_of_memory);
    } catch (const decode_status_exception &error) { io.fail(error.status); } catch (...) {
        io.fail(status_code::error);
    }
}

template <typename Arg> void accept_input(resumable_io &io, resumable_feed_result &result, Arg &&arg) {
    if constexpr (input_chunk_arg<Arg>) {
        io.input.emplace_back(std::forward<Arg>(arg));
        ++result.accepted_chunks;
    } else {
        for (auto &&chunk : arg) {
            io.input.emplace_back(std::forward<decltype(chunk)>(chunk));
            ++result.accepted_chunks;
        }
    }
}

template <typename Arg> void accept_output(resumable_io &io, resumable_feed_result &result, Arg &&arg) {
    if constexpr (output_chunk_arg<Arg>) {
        io.output.emplace_back(std::forward<Arg>(arg));
        io.output.back().reset_begin();
        ++result.accepted_chunks;
    } else {
        for (auto &&chunk : arg) {
            io.output.emplace_back(std::forward<decltype(chunk)>(chunk));
            io.output.back().reset_begin();
            ++result.accepted_chunks;
        }
    }
}

template <typename Arg> void accept_lease(resumable_io &io, resumable_feed_result &result, Arg &&arg) {
    if constexpr (leased_output_arg<Arg>) {
        io.output.emplace_back(std::move(arg.chunk));
        io.output.back().reset_begin();
        ++result.accepted_chunks;
    } else {
        for (auto &&lease : arg) {
            io.output.emplace_back(std::move(lease.chunk));
            io.output.back().reset_begin();
            ++result.accepted_chunks;
        }
    }
}

template <typename Arg> void accept_generator_output(resumable_io &io, resumable_feed_result &result, Arg &&arg) {
    if constexpr (leased_output_arg<Arg> || leased_output_batch<Arg>) {
        accept_lease(io, result, std::forward<Arg>(arg));
    } else {
        accept_output(io, result, std::forward<Arg>(arg));
    }
}

template <std::size_t I = 0, typename Tuple> void accept_inputs(resumable_io &io, resumable_feed_result &result, Tuple &args) {
    if constexpr (I < std::tuple_size_v<Tuple>) {
        using element_type = std::tuple_element_t<I, Tuple>;
        auto &&arg         = std::get<I>(args);
        if constexpr (std::same_as<std::remove_cvref_t<decltype(arg)>, end_of_input_t>) {
            static_assert(I + 1U == std::tuple_size_v<Tuple>, "end_of_input must be the last feed argument");
            io.eof = true;
        } else {
            accept_input(io, result, std::forward<element_type>(arg));
        }
        accept_inputs<I + 1U>(io, result, args);
    }
}

inline void cancel_state(const std::shared_ptr<resumable_io> &io) noexcept {
    if (!io || io->terminal)
        return;
    io->fail(status_code::cancelled);
    // A customization can cancel or destroy its operation while the leaf is
    // running. The driver owns the frame until control returns to it.
    if (io->driving)
        return;
    io->root    = {};
    auto waiter = io->take_ready_waiter();
    if (waiter)
        waiter.resume();
}

struct feeding_guard {
    std::shared_ptr<resumable_io> io;
    explicit feeding_guard(std::shared_ptr<resumable_io> state) : io(std::move(state)) { io->feeding = true; }
    ~feeding_guard() { io->feeding = false; }
};

struct result_awaiter {
    std::shared_ptr<resumable_io> io;
    std::coroutine_handle<>       awaiting{};
    bool                          registered{};
    bool                          conflict{};

    result_awaiter(const result_awaiter &) = delete;
    result_awaiter(result_awaiter &&other) noexcept
        : io(std::move(other.io)), awaiting(other.awaiting), registered(std::exchange(other.registered, false)), conflict(other.conflict) {}
    explicit result_awaiter(std::shared_ptr<resumable_io> value) : io(std::move(value)) {}
    ~result_awaiter() {
        if (registered && io && io->waiter == awaiting) {
            io->waiter      = {};
            io->waiting_for = resumable_waiter::none;
        }
    }
    [[nodiscard]] bool await_ready() {
        if (!io)
            return true;
        try {
            io->drive();
        } catch (...) { classify_exception(*io); }
        return io->terminal;
    }
    bool await_suspend(std::coroutine_handle<> handle) noexcept {
        if (io->waiter) {
            conflict = true;
            return false;
        }
        awaiting        = handle;
        registered      = true;
        io->waiter      = handle;
        io->waiting_for = resumable_waiter::result;
        return true;
    }
    [[nodiscard]] expected<std::size_t, status_code> await_resume() {
        registered = false;
        if (!io)
            return unexpected<status_code>{status_code::out_of_memory};
        if (conflict)
            return unexpected<status_code>{status_code::unsupported_operation};
        return io->result();
    }
};

} // namespace detail

template <detail::resumable_direction Direction> class resumable_operation {
  protected:
    std::shared_ptr<detail::resumable_io> io_;

  public:
    explicit resumable_operation(std::shared_ptr<detail::resumable_io> io) : io_(std::move(io)) {}
    resumable_operation(const resumable_operation &)            = delete;
    resumable_operation &operator=(const resumable_operation &) = delete;
    resumable_operation(resumable_operation &&) noexcept        = default;
    resumable_operation &operator=(resumable_operation &&other) noexcept {
        auto &result = *this;
        if (this != &other) {
            // Terminal delivery may destroy either owner. Finish the transfer
            // before notifying and retain the previous state independently.
            auto previous = std::exchange(io_, std::move(other.io_));
            detail::cancel_state(previous);
        }
        return result;
    }
    ~resumable_operation() { detail::cancel_state(io_); }

    [[nodiscard]] bool                               done() const noexcept { return !io_ || io_->terminal; }
    [[nodiscard]] resumable_state                    state() const noexcept { return io_ ? io_->state() : resumable_state::failed; }
    [[nodiscard]] expected<std::size_t, status_code> result() const {
        assert(done());
        return io_ ? io_->result() : expected<std::size_t, status_code>{unexpected<status_code>{status_code::out_of_memory}};
    }
    [[nodiscard]] std::span<const readable_chunk> remaining_input() const noexcept {
        if (!io_)
            return {};
        return std::span<const readable_chunk>{io_->input}.subspan(io_->input_index);
    }
    [[nodiscard]] std::span<const writable_chunk> remaining_output() const noexcept {
        if constexpr (Direction == detail::resumable_direction::output_generator) {
            return io_ ? std::span<const writable_chunk>{io_->output}.subspan(io_->output_index) : std::span<const writable_chunk>{};
        }
        return io_ && io_->last_output ? std::span<const writable_chunk>{*io_->last_output}.subspan(io_->last_output_first)
                                       : std::span<const writable_chunk>{};
    }
    void                                 cancel() noexcept { detail::cancel_state(io_); }
    [[nodiscard]] detail::result_awaiter operator co_await() const noexcept { return detail::result_awaiter{io_}; }

    template <typename... Args>
        requires(Direction == detail::resumable_direction::input && (detail::input_feed_arg<Args> && ...))
    [[nodiscard]] resumable_feed_result operator()(Args &&...args) {
        resumable_feed_result result;
        auto                  io = io_;
        if (!io)
            return {.state = resumable_state::failed, .error = status_code::out_of_memory, .output_chunks = {}};
        if (io->terminal || io->driving || io->feeding || io->eof) {
            result.state = io->state();
            result.error = status_code::unsupported_operation;
            return result;
        }
        detail::feeding_guard guard{io};
        const auto            start = io->total;
        try {
            // Drop consumed metadata without walking any unread byte range.
            io->input.erase(io->input.begin(), io->input.begin() + static_cast<std::ptrdiff_t>(io->input_index));
            io->input_index = 0;
            auto supplied   = std::forward_as_tuple(std::forward<Args>(args)...);
            detail::accept_inputs(*io, result, supplied);
            ++io->input_generation;
            io->drive();
        } catch (...) { detail::classify_exception(*io); }
        auto waiter = io->take_ready_waiter();
        if (waiter)
            waiter.resume();
        // The resumed consumer may acknowledge several already-buffered items.
        result.bytes = io->total - start;
        result.state = io->state();
        if (io->terminal)
            result.error = io->status;
        return result;
    }

    template <typename... Args>
        requires(Direction == detail::resumable_direction::output && (detail::output_feed_arg<Args> && ...))
    [[nodiscard]] resumable_feed_result operator()(Args &&...args) {
        resumable_feed_result result;
        auto                  io = io_;
        if (!io)
            return {.state = resumable_state::failed, .error = status_code::out_of_memory, .output_chunks = {}};
        if (io->terminal || io->driving || io->feeding) {
            result.state = io->state();
            result.error = status_code::unsupported_operation;
            return result;
        }
        detail::feeding_guard                        guard{io};
        const auto                                   start = io->total;
        std::shared_ptr<std::vector<writable_chunk>> completed;
        try {
            completed = std::make_shared<std::vector<writable_chunk>>();
            io->output.clear();
            io->output_index   = 0;
            io->output_written = 0;
            (detail::accept_output(*io, result, std::forward<Args>(args)), ...);
            io->drive();
        } catch (...) { detail::classify_exception(*io); }
        if (completed) {
            completed->swap(io->output);
            result.output_chunks = completed;
            io->last_output      = completed;
            result.first_unused  = io->output_index;
            while (result.first_unused < completed->size() && (*completed)[result.first_unused].empty())
                ++result.first_unused;
            io->last_output_first = result.first_unused;
        }
        auto waiter = io->take_ready_waiter();
        if (waiter)
            waiter.resume();
        result.bytes = io->total - start;
        result.state = io->state();
        if (io->terminal)
            result.error = io->status;
        return result;
    }

    template <typename... Args>
        requires(Direction == detail::resumable_direction::output_generator &&
                 ((detail::leased_output_arg<Args> || detail::leased_output_batch<Args> || detail::output_feed_arg<Args>) && ...))
    [[nodiscard]] resumable_feed_result operator()(Args &&...args) {
        resumable_feed_result result;
        auto                  io = io_;
        if (!io)
            return {.state = resumable_state::failed, .error = status_code::out_of_memory, .output_chunks = {}};
        if (io->terminal || io->driving || io->feeding) {
            result.state = io->state();
            result.error = status_code::unsupported_operation;
            return result;
        }
        detail::feeding_guard guard{io};
        const auto            start = io->total;
        try {
            io->discard_consumed_output();
            (detail::accept_generator_output(*io, result, std::forward<Args>(args)), ...);
            io->drive();
        } catch (...) { detail::classify_exception(*io); }
        auto waiter = io->take_ready_waiter();
        if (waiter)
            waiter.resume();
        result.bytes = io->total - start;
        result.state = io->state();
        if (io->terminal)
            result.error = io->status;
        return result;
    }
};

template <typename T, detail::resumable_direction Direction> class resumable_generator : public resumable_operation<Direction> {
    using base = resumable_operation<Direction>;
    std::shared_ptr<std::optional<T>> value_;

    struct next_awaiter {
        std::shared_ptr<detail::resumable_io> io;
        std::shared_ptr<std::optional<T>>     value;
        std::coroutine_handle<>               awaiting{};
        bool                                  registered{};

        next_awaiter(const next_awaiter &) = delete;
        next_awaiter(next_awaiter &&other) noexcept
            : io(std::move(other.io)), value(std::move(other.value)), awaiting(other.awaiting),
              registered(std::exchange(other.registered, false)) {}
        next_awaiter(std::shared_ptr<detail::resumable_io> state, std::shared_ptr<std::optional<T>> slot)
            : io(std::move(state)), value(std::move(slot)) {}
        ~next_awaiter() {
            if (registered && io && io->waiter == awaiting) {
                io->waiter      = {};
                io->waiting_for = detail::resumable_waiter::none;
            }
        }
        [[nodiscard]] bool await_ready() {
            if (!io)
                return true;
            if (io->yielded && io->yielded_claimed) {
                if (value)
                    value->reset();
                io->acknowledge();
            }
            try {
                io->drive();
            } catch (...) { detail::classify_exception(*io); }
            return io->yielded || io->terminal;
        }
        bool await_suspend(std::coroutine_handle<> handle) noexcept {
            if (io->waiter) {
                io->fail(status_code::unsupported_operation);
                auto waiter = io->take_ready_waiter();
                if (waiter)
                    waiter.resume();
                return false;
            }
            awaiting        = handle;
            registered      = true;
            io->waiter      = handle;
            io->waiting_for = detail::resumable_waiter::next;
            return true;
        }
        [[nodiscard]] bool await_resume() noexcept {
            registered = false;
            if (io && io->yielded)
                io->yielded_claimed = true;
            return io && io->yielded;
        }
    };

  public:
    resumable_generator(std::shared_ptr<detail::resumable_io> io, std::shared_ptr<std::optional<T>> value)
        : base(std::move(io)), value_(std::move(value)) {}
    resumable_generator(resumable_generator &&) noexcept = default;
    resumable_generator &operator=(resumable_generator &&other) noexcept {
        auto &result = *this;
        if (this != &other) {
            // The base assignment cannot notify before the derived value slot
            // is installed: the old consumer may destroy this generator inline.
            auto previous = std::exchange(this->io_, std::move(other.io_));
            value_        = std::move(other.value_);
            detail::cancel_state(previous);
        }
        return result;
    }
    [[nodiscard]] next_awaiter next() { return next_awaiter{this->io_, value_}; }
    [[nodiscard]] const T     &value() const {
        assert(this->io_ && this->io_->yielded);
        if constexpr (Direction == detail::resumable_direction::output_generator) {
            return *this->io_->yielded_output;
        } else {
            return **value_;
        }
    }
};

} // namespace cbor::tags
