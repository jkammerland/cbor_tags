#include <complex>
#include <string>

struct alpha {
    std::complex<double> a;
    std::string          b;

    std::string print(int) const { return "alpha" + std::to_string(a.real()) + std::to_string(a.imag()) + b; }
};
