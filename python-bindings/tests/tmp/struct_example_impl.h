#include <string>

namespace test2 {

struct A {
    int         a1;
    double      a2;
    std::string a3;
};

// Function that takes struct A as input
void processA(const A &);

struct B {
    std::string b;
};

// Function that takes struct B as input
void processB(const B &);

} // namespace test2

namespace test {

struct A;

struct B;

} // namespace test
