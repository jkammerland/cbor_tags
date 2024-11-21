#include "struct_example.h"

#include <cbor_tags/cbor_encoder.h>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <pybind11/pybind11.h>

template <typename T> inline std::string to_hex(const T &bytes) {
    std::string hex;
    hex.reserve(bytes.size() * 2);

    fmt::format_to(std::back_inserter(hex), "{:02x}", fmt::join(bytes, ""));

    return hex;
}

// Function that takes struct A as input
void processA(const A &input) {
    fmt::print("processA: a1={}, a2={}, a3={}\n", input.a1, input.a2, input.a3);
    auto data = std::vector<std::byte>{};
    auto enc  = cbor::tags::make_encoder(data);
    enc(input);

    fmt::print("Data: {}\n", to_hex(data));
}

// Function that takes struct B as input
void processB(const B &input) { fmt::print("processB: b={}\n", input.b); }

namespace py = pybind11;
PYBIND11_MODULE(pybinding1, m) {
    // Expose struct A
    py::class_<A>(m, "A")
        .def(py::init<>()) // Default constructor
        .def_readwrite("a1", &A::a1)
        .def_readwrite("a2", &A::a2)
        .def_readwrite("a3", &A::a3);

    // Expose struct B
    py::class_<B>(m, "B")
        .def(py::init<>()) // Default constructor
        .def_readwrite("b", &B::b);

    // Expose function that takes struct A
    m.def("processA", &processA, "Function to process struct A");

    // Expose function that takes struct B
    m.def("processB", &processB, "Function to process struct B");
}
