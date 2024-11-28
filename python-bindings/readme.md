# Input example
```cpp
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
};

// Function that takes struct B as input
void processB(const B &) {}

} // namespace test

int main() { return 0; }
```


# generator stdout

binding_generator
argv[1] = struct_example_impl.cpp
argv[2] = --
argv[3] = -std=c++14
argv[4] = -I/usr/lib/gcc/x86_64-redhat-linux/14/include
argv[5] = -I/usr/include/c++/14
argv[6] = -I/usr/include/c++/14/x86_64-redhat-linux
argv[7] = -I/usr/include/c++/14/backward
argv[8] = -I/usr/local/include
argv[9] = -I/usr/include
Creating processing action for file: ...../struct_example_impl.cpp
Struct: A
Function: processA
Struct: B
Function: processB
Destroying StructVisitor
Destroying BindingGeneratorAction

//.... Incorrect filtering

Struct: __is_fast_hash
Struct: __is_fast_hash
Struct: _Guard
    basic_string<_CharT, _Traits, _Alloc> * _M_guarded
Struct: _Guard
    basic_string<_CharT, _Traits, _Alloc> * _M_guarded
Struct: _Terminator
    basic_string<_CharT, _Traits, _Alloc> * _M_this
    size_type _M_r
Struct: EmptyOuter
Struct: A
    int a1
    double a2
    std::string a3
    C c
    EmptyOuter eo
Struct: B
Struct: C
    int c1
    int c2
Struct: B
    std::string b
    A a
    struct A::B ab
Function: processA
    Return type: void
    const A & 
Function: processB
    Return type: void
    const B &

# cpp file output

```cpp
    // Incorrect filtering ...

    py::class_<_Guard>(m, "_Guard")
        .def(py::init<>())
        .def_readwrite("_M_guarded", &_Guard::_M_guarded)
        ;

    py::class_<_Guard>(m, "_Guard")
        .def(py::init<>())
        .def_readwrite("_M_guarded", &_Guard::_M_guarded)
        ;

    py::class_<_Terminator>(m, "_Terminator")
        .def(py::init<>())
        .def_readwrite("_M_this", &_Terminator::_M_this)
        .def_readwrite("_M_r", &_Terminator::_M_r)
        ;

    py::class_<EmptyOuter>(m, "EmptyOuter")
        .def(py::init<>())
        ;

    py::class_<A>(m, "A")
        .def(py::init<>())
        .def_readwrite("a1", &A::a1)
        .def_readwrite("a2", &A::a2)
        .def_readwrite("a3", &A::a3)
        .def_readwrite("c", &A::c)
        .def_readwrite("eo", &A::eo)
        ;

    py::class_<B>(m, "B")
        .def(py::init<>())
        ;

    py::class_<C>(m, "C")
        .def(py::init<>())
        .def_readwrite("c1", &C::c1)
        .def_readwrite("c2", &C::c2)
        ;

    py::class_<B>(m, "B")
        .def(py::init<>())
        .def_readwrite("b", &B::b)
        .def_readwrite("a", &B::a)
        .def_readwrite("ab", &B::ab)
        ;

    m.def("processA", &processA);
    m.def("processB", &processB);

```
