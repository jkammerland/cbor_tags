#pragma once

#include <initializer_list>
#include <utility>

namespace expected_test {
struct failure {};
struct counts {
    int  live{};
    bool fail_copy{};
    bool fail_move{};
    bool fail_assign{};
};

template <bool NothrowMove> struct tracked {
    counts *state;
    int     number;
    tracked(counts &state, int number) noexcept : state(&state), number(number) { ++state.live; }
    tracked(const tracked &other) : state(other.state), number(other.number) {
        if (state->fail_copy)
            throw failure{};
        ++state->live;
    }
    tracked(tracked &&other) noexcept(NothrowMove) : state(other.state), number(other.number) {
        if constexpr (!NothrowMove) {
            if (state->fail_move)
                throw failure{};
        }
        ++state->live;
        other.number = -1;
    }
    tracked &operator=(const tracked &other) {
        if (state->fail_assign)
            throw failure{};
        number = other.number;
        return *this;
    }
    tracked &operator=(tracked &&other) noexcept(NothrowMove) {
        if constexpr (!NothrowMove) {
            if (state->fail_assign)
                throw failure{};
        }
        number = std::exchange(other.number, -1);
        return *this;
    }
    ~tracked() { --state->live; }
    friend void swap(tracked &left, tracked &right) noexcept {
        using std::swap;
        swap(left.number, right.number);
    }
};

struct immovable {
    int number;
    constexpr explicit immovable(int number) noexcept : number(number) {}
    immovable(const immovable &)            = delete;
    immovable(immovable &&)                 = delete;
    immovable &operator=(const immovable &) = delete;
    immovable &operator=(immovable &&)      = delete;
};
struct explicit_int {
    int number;
    constexpr explicit explicit_int(int number) noexcept : number(number) {}
};
struct copy_only {
    int number;
    constexpr explicit copy_only(int number = 0) noexcept : number(number) {}
    copy_only(const copy_only &)            = default;
    copy_only(copy_only &&)                 = delete;
    copy_only &operator=(const copy_only &) = default;
    copy_only &operator=(copy_only &&)      = delete;
};
struct list_sum {
    int number;
    constexpr list_sum(std::initializer_list<int> values, int offset = 0) noexcept : number(offset) {
        for (int value : values)
            number += value;
    }
};
struct qualifier {
    constexpr int operator()(int &) const { return 1; }
    constexpr int operator()(const int &) const { return 2; }
    constexpr int operator()(int &&) const { return 3; }
    constexpr int operator()(const int &&) const { return 4; }
};
struct rvalue_callback {
    constexpr int operator()(int n) && { return n + 1; }
    int           operator()(int) & = delete;
};
} // namespace expected_test
