#pragma once
#include <cstdint>
#include <limits>

namespace rng {

class small_generator {
  public:
    using state_type  = uint32_t;
    using result_type = uint64_t;

    small_generator(state_type seed) { init(seed); }

    static constexpr state_type min() { return 0; }

    static constexpr state_type max() { return std::numeric_limits<state_type>::max(); }

    [[nodiscard]] result_type operator()() {
        auto r1 = static_cast<result_type>(generate());
        auto r2 = static_cast<result_type>(generate());
        return (r2 << 32) | r1;
    }

    state_type generate() {
        state_type e = a - rot(b, 27);
        a            = b ^ rot(c, 17);
        b            = c + d;
        c            = d + e;
        d            = e + a;
        return d;
    }

  private:
    void init(state_type seed) {
        a = 0xf1ea5eed, b = c = d = seed;
        for (state_type i = 0; i < 20; ++i) {
            [[maybe_unused]] auto _ = this->operator()();
        }
    }

    state_type      a, b, c, d;
    static uint64_t rot(state_type x, state_type k) { return ((x) << (k)) | ((x) >> (32 - (k))); }
};

} // namespace rng
