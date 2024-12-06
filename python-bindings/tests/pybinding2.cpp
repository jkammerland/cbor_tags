#include <pybind11/complex.h>
#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

// User headers
#include "/home/jonkam/Programming/cpp/cbor_tags/build/python-bindings/another_ex.h"

// System headers#include <complex>
#include <cstdint>
#include <string>

namespace py = pybind11;

PYBIND11_MODULE(pybinding2, m) {
    py::class_<n1::ComplexWrapper> ComplexWrapper_class(m, "ComplexWrapper");
    py::class_<n1::n2::n3::alpha>  alpha_class(m, "alpha");
    py::enum_<n1::n2::n3::alpha::zeta>(m, "zeta", py::arithmetic())
        .value("A", n1::n2::n3::alpha::zeta::A)
        .value("B", n1::n2::n3::alpha::zeta::B)
        .value("C", n1::n2::n3::alpha::zeta::C)
        .export_values();

    py::class_<n1::n2::n3::alpha::Nested> Nested_class(m, "Nested");
    py::class_<n1::n2::n3::alpha::empty>  empty_class(m, "empty");
    py::enum_<n1::beta>(m, "beta", py::arithmetic())
        .value("A", n1::beta::A)
        .value("B", n1::beta::B)
        .value("C", n1::beta::C)
        .export_values();

    ComplexWrapper_class.def(py::init<>()).def_readwrite("c", &n1::ComplexWrapper::c);

    alpha_class.def(py::init<>())
        .def_readwrite("a", &n1::n2::n3::alpha::a)
        .def_readwrite("b", &n1::n2::n3::alpha::b)
        .def_readwrite("n", &n1::n2::n3::alpha::n)
        .def_readwrite("z", &n1::n2::n3::alpha::z)
        .def("print", &n1::n2::n3::alpha::print);

    Nested_class.def(py::init<>()).def_readwrite("cw", &n1::n2::n3::alpha::Nested::cw).def("print", &n1::n2::n3::alpha::Nested::print);

    empty_class.def(py::init<>());
}
