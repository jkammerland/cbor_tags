#include <complex>
#include <cstdint>
#include <string>

namespace n1 {

namespace n2::n3 {

struct alpha {
    std::complex<double> a;
    std::string          b;

    std::string print(int imag) const {
        auto f = [](int x) { return x + 1; };
        return "alpha" + std::to_string(a.real()) + std::to_string(a.imag()) + std::to_string(f(imag)) + b;
    }

    enum class zeta : std::uint16_t { A = 1, B, C };

    zeta z;
};

} // namespace n2::n3

enum class beta : int { A = -1, B, C };

}; // namespace n1
