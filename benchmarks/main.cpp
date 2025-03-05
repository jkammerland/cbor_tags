#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest/doctest.h"

#include <iostream>
#include <nanobench.h>

// A simple function to benchmark
int fibonacci(int n) {
    if (n <= 1)
        return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

// Doctest test cases
TEST_CASE("testing the fibonacci function") {
    CHECK(fibonacci(0) == 0);
    CHECK(fibonacci(1) == 1);
    CHECK(fibonacci(2) == 1);
    CHECK(fibonacci(3) == 2);
    CHECK(fibonacci(10) == 55);
}

// Function to run nanobench benchmark
void run_benchmark() {
    ankerl::nanobench::Bench bench;
    bench.minEpochIterations(100);

    bench.run("fibonacci(20)", [&] { ankerl::nanobench::doNotOptimizeAway(fibonacci(20)); });
    bench.run("fibonacci(30)", [&] { ankerl::nanobench::doNotOptimizeAway(fibonacci(30)); });
    bench.run("fibonacci(40)", [&] { ankerl::nanobench::doNotOptimizeAway(fibonacci(31)); });
}

TEST_CASE("benchmarking the fibonacci function") { run_benchmark(); }

// Main function to run tests and benchmarks
int main(int argc, char **argv) {
    // Run Doctest tests
    int result = doctest::Context(argc, argv).run();
    return result;
}