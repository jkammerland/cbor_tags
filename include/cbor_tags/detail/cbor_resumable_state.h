#pragma once

#include "cbor_tags/detail/cbor_resumable_chunks.h"
#include "cbor_tags/detail/cbor_resumable_task.h"

#include <coroutine>
#include <cstddef>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cbor::tags {

enum class resumable_state { need_input, need_output, yielded, done, failed };

struct end_of_input_t {};
inline constexpr end_of_input_t end_of_input{};

struct resumable_feed_result {
    resumable_state state{resumable_state::need_input};
    std::size_t     bytes{};
    std::size_t     accepted_chunks{};
    status_code     error{status_code::success};
    std::size_t     first_unused{};
    // The descriptors outlive the feed call; the underlying buffers remain
    // caller-owned. Prefix views are materialized only when requested.
    std::shared_ptr<const std::vector<writable_chunk>> output_chunks;

    [[nodiscard]] auto produced() const {
        const auto chunks = output_chunks ? std::span<const writable_chunk>{*output_chunks} : std::span<const writable_chunk>{};
        return chunks | std::views::transform([](const writable_chunk &chunk) { return chunk.produced(); });
    }

    [[nodiscard]] std::span<const writable_chunk> unused_output() const noexcept {
        return output_chunks ? std::span<const writable_chunk>{*output_chunks}.subspan(first_unused) : std::span<const writable_chunk>{};
    }
};

namespace detail {

enum class resumable_direction { input, output, output_generator };
enum class resumable_block { none, input, more_input, output, yield };
enum class resumable_waiter { none, result, next };

struct resumable_io {
    resumable_direction                                direction{resumable_direction::input};
    std::vector<readable_chunk>                        input;
    std::vector<writable_chunk>                        output;
    std::size_t                                        input_index{};
    std::size_t                                        output_index{};
    std::size_t                                        output_written{};
    std::size_t                                        total{};
    std::size_t                                        input_generation{};
    std::size_t                                        wait_generation{};
    bool                                               eof{};
    bool                                               started{};
    bool                                               driving{};
    bool                                               feeding{};
    bool                                               yielded{};
    bool                                               yielded_claimed{};
    bool                                               terminal{};
    bool                                               root_finished{};
    status_code                                        status{status_code::success};
    resumable_block                                    blocked{resumable_block::none};
    std::coroutine_handle<>                            leaf{};
    std::coroutine_handle<>                            waiter{};
    resumable_waiter                                   waiting_for{resumable_waiter::none};
    std::optional<readable_chunk>                      yielded_output;
    std::shared_ptr<const std::vector<writable_chunk>> last_output;
    std::size_t                                        last_output_first{};

    // Destroy the coroutine before the state and borrowed cursor descriptors.
    resumable_task<status_code> root;

    [[nodiscard]] bool has_input() {
        while (input_index < input.size() && input[input_index].empty())
            ++input_index;
        return input_index < input.size();
    }

    [[nodiscard]] bool has_output() {
        while (output_index < output.size() && output[output_index].empty())
            ++output_index;
        return output_index < output.size();
    }

    [[nodiscard]] bool prepare_output() {
        if (direction == resumable_direction::output_generator && output_index < output.size() && output[output_index].empty() &&
            output_written != 0U) {
            yielded_output.emplace(output[output_index].produced());
            ++output_index;
            output_written  = 0;
            yielded         = true;
            yielded_claimed = false;
            return false;
        }
        return has_output();
    }

    void count_byte() {
        check_cancelled();
        if (total == std::numeric_limits<std::size_t>::max()) {
            throw decode_status_exception{status_code::size_limit_exceeded};
        }
        ++total;
    }

    void check_cancelled() const {
        if (terminal && status == status_code::cancelled)
            throw decode_status_exception{status_code::cancelled};
    }

    [[nodiscard]] resumable_state state() const noexcept {
        if (yielded)
            return resumable_state::yielded;
        if (terminal)
            return status == status_code::success ? resumable_state::done : resumable_state::failed;
        return direction == resumable_direction::input ? resumable_state::need_input : resumable_state::need_output;
    }

    [[nodiscard]] expected<std::size_t, status_code> result() const {
        if (status != status_code::success)
            return unexpected<status_code>{status};
        return total;
    }

    void fail(status_code reason) noexcept {
        status   = reason;
        terminal = true;
        yielded  = false;
        leaf     = {};
    }

    void finish_root() {
        root_finished = true;
        try {
            status = root.result();
        } catch (const std::bad_alloc &) { status = status_code::out_of_memory; } catch (const std::length_error &) {
            status = status_code::out_of_memory;
        } catch (const decode_status_exception &error) { status = error.status; } catch (...) {
            status = status_code::error;
        }

        if (direction == resumable_direction::output_generator && output_written != 0U) {
            yielded_output.emplace(output[output_index].produced());
            output_written = 0;
            // A final partial buffer still has usable caller-owned capacity.
            if (output[output_index].empty())
                ++output_index;
            yielded         = true;
            yielded_claimed = false;
        } else {
            terminal = true;
        }
    }

    void drive() {
        if (terminal || yielded || driving || !root.valid())
            return;
        if (root_finished) {
            terminal = true;
            return;
        }
        if (started) {
            if (blocked == resumable_block::input && !has_input() && !eof)
                return;
            if (blocked == resumable_block::more_input && input_generation == wait_generation && !eof)
                return;
            if (blocked == resumable_block::output && !has_output())
                return;
        }

        driving   = true;
        auto next = started ? std::exchange(leaf, {}) : std::coroutine_handle<>{root.handle()};
        started   = true;
        blocked   = resumable_block::none;
        if (next)
            next.resume();
        driving = false;
        if (terminal && status == status_code::cancelled) {
            root = {};
            return;
        }
        if (root.done())
            finish_root();
    }

    [[nodiscard]] std::coroutine_handle<> take_ready_waiter() noexcept {
        if (!waiter)
            return {};
        const bool ready = waiting_for == resumable_waiter::result ? terminal : (yielded || terminal);
        if (!ready)
            return {};
        waiting_for = resumable_waiter::none;
        return std::exchange(waiter, {});
    }

    void discard_consumed_output() noexcept {
        // The published readable cursor owns its own iterator copies. Drop
        // only complete buffers, never the current partially written one.
        output.erase(output.begin(), output.begin() + static_cast<std::ptrdiff_t>(output_index));
        output_index = 0;
    }

    void acknowledge() {
        yielded         = false;
        yielded_claimed = false;
        yielded_output.reset();
        if (direction == resumable_direction::output_generator)
            discard_consumed_output();
        if (root_finished)
            terminal = true;
    }
};

struct resumable_read_byte {
    resumable_io      &io;
    [[nodiscard]] bool await_ready() const { return io.terminal || io.has_input() || io.eof; }
    void               await_suspend(std::coroutine_handle<> handle) const noexcept {
        io.leaf    = handle;
        io.blocked = resumable_block::input;
    }
    [[nodiscard]] std::byte await_resume() const {
        io.check_cancelled();
        if (!io.has_input())
            throw decode_status_exception{status_code::incomplete};
        io.count_byte();
        return io.input[io.input_index].read();
    }
};

struct resumable_write_byte {
    resumable_io      &io;
    std::byte          value;
    [[nodiscard]] bool await_ready() const { return io.terminal || io.prepare_output(); }
    void               await_suspend(std::coroutine_handle<> handle) const noexcept {
        io.leaf    = handle;
        io.blocked = resumable_block::output;
    }
    void await_resume() const {
        io.count_byte();
        io.output[io.output_index].write(value);
        ++io.output_written;
    }
};

struct resumable_wait_more_input {
    resumable_io      &io;
    [[nodiscard]] bool await_ready() const noexcept { return io.terminal; }
    void               await_suspend(std::coroutine_handle<> handle) const noexcept {
        io.leaf            = handle;
        io.blocked         = resumable_block::more_input;
        io.wait_generation = io.input_generation;
    }
    void await_resume() const { io.check_cancelled(); }
};

struct resumable_yield {
    resumable_io      &io;
    [[nodiscard]] bool await_ready() const noexcept { return io.terminal; }
    void               await_suspend(std::coroutine_handle<> handle) const noexcept {
        io.yielded         = true;
        io.yielded_claimed = false;
        io.leaf            = handle;
        io.blocked         = resumable_block::yield;
    }
    void await_resume() const { io.check_cancelled(); }
};

} // namespace detail
} // namespace cbor::tags
