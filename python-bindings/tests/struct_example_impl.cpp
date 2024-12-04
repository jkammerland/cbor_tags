#include "tmp/struct_example_impl.h"

#include <string>

namespace test {

struct EmptyOuter {}; // empty outer definition

struct A {
    int         a1;
    double      a2;
    std::string a3;

    struct B {}; // Unused (empty) inner definition, should NOT appear in A

    struct C {
        int c1;
        int c2;
    };

    C c; // Inner struct

    EmptyOuter eo; // Outer struct
};

// Function that takes struct A as input
void processA(const A &) {}

struct B {
    std::string b;
    A           a;
    A::B        ab; // Should appear in B
    A::C        ac; // Should appear in B
};

// Function that takes struct B as input
void processB(const B &) {}

} // namespace test

int main() { return 0; }