#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "another_ex.h"

#include <complex>
#include <cstdint>
#include <string>

namespace py = pybind11;

PYBIND11_MODULE(pybinding2, m) {
    py::class_<n1::n2::n3::alpha> alpha_class(m, "alpha");
    py::enum_<n1::n2::n3::alpha::zeta>(m, "zeta")
        .value("A", n1::n2::n3::alpha::zeta::A)
        .value("B", n1::n2::n3::alpha::zeta::B)
        .value("C", n1::n2::n3::alpha::zeta::C)
        .export_values();

    py::enum_<n1::beta>(m, "beta")
        .value("A", n1::beta::A)
        .value("B", n1::beta::B)
        .value("C", n1::beta::C)
        .export_values();


    alpha_class
        .def(py::init<>())
        .def_readwrite("a", &n1::n2::n3::alpha::a)
        .def_readwrite("b", &n1::n2::n3::alpha::b)
        .def_readwrite("z", &n1::n2::n3::alpha::z)
        .def("print", &n1::n2::n3::alpha::print)
        ;

}
