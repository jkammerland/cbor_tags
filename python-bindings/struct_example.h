#pragma once

#include <string>

struct A {
    int         a1;
    double      a2;
    std::string a3;
};

// Function that takes struct A as input
void processA(const A &input);

struct B {
    std::string b;
};

// Function that takes struct B as input
void processB(const B &input);
