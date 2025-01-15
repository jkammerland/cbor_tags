#include <cstdint>
#include <limits>

namespace rng {

class small_generator {
  public:
    using result_type = uint64_t;

    small_generator(result_type seed) { init(seed); }

    static constexpr result_type min() { return 0; }

    static constexpr result_type max() { return std::numeric_limits<result_type>::max(); }

    result_type operator()() {
        result_type e = a - rot(b, 27);
        a             = b ^ rot(c, 17);
        b             = c + d;
        c             = d + e;
        d             = e + a;
        return d;
    }

  private:
    void init(result_type seed) {
        a = 0xf1ea5eed, b = c = d = seed;
        for (result_type i = 0; i < 20; ++i) {
            this->operator()();
        }
    }

    result_type            a, b, c, d;
    static inline uint64_t rot(result_type x, result_type k) { return ((x) << (k)) | ((x) >> (32 - (k))); }
};

} // namespace rng
