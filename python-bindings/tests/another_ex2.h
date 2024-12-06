#include <complex>
#include <cstdint>
#include <string>

namespace n1 {

using Complex = std::complex<float>;

struct ComplexWrapper {
    Complex c;
};

namespace n2::n3 {

struct alpha {
    std::complex<double> a;
    std::string          b;

    std::string print(int imag) const {
        auto f = [](int x) { return x + 1; };
        return "alpha" + std::to_string(a.real()) + std::to_string(a.imag()) + std::to_string(f(imag)) + b;
    }

    enum class zeta : std::uint16_t { A = 1, B, C };

    struct Nested {
        // Same name as the outer class, try to complicate things
        std::string print(int real) { return std::to_string(real); }

        ComplexWrapper cw;
    };

    struct empty {};

    Nested n;

    zeta z;
};

} // namespace n2::n3

enum class beta : int { A = -1, B, C };

}; // namespace n1
