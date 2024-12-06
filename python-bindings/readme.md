# Working example python output

python main2.py 
Alpha print result: alpha1.0000002.0000006test
Nested print result: 10
Nested cw.c: (5+6j)
Beta A value: -1
zeta.A: 1
zeta.B: 2
zeta.C: 3
jonkam@7950x:tests$ python main2.py 
Alpha print result: alpha1.0000002.0000006test
Nested print result: 10
Nested cw.c: (5+6j)
Beta A value: -1
Beta B value: 0
Beta C value: 1
zeta.A: 1
zeta.B: 2
zeta.C: 3

```py
from pybinding2 import ComplexWrapper, alpha, zeta, Nested, empty, beta

def main():
    # Create an instance of alpha
    a = alpha()
    
    # Set complex number
    a.a = complex(1.0, 2.0)
    
    # Set string
    a.b = "test"
    
    # Set enum value
    a.z = zeta.B
    
    # Call print method
    print("Alpha print result:", a.print(5))  # Should print real part, imaginary part, f(imag), and string b
    
    # Create and use Nested class
    nested = Nested()
    print("Nested print result:", nested.print(10))
    
    # Work with ComplexWrapper
    wrapper = ComplexWrapper()
    wrapper.c = complex(3.0, 4.0)
    
    # Assign ComplexWrapper to nested.cw
    nested.cw = wrapper
    nested.cw.c = complex(5.0, 6.0)
    print("Nested cw.c:", nested.cw.c)  # Should print 5.0 + 6.0j
    
    # Create empty class instance
    e = empty()
    
    # Work with beta enum
    b = beta.A
    print("Beta A value:", int(b))  # Should print -1
    print("Beta B value:", int(beta.B))  # Should print 0
    print("Beta C value:", int(beta.C))  # Should print 1
    
    # Print enum values
    print("zeta.A:", int(zeta.A))  # Should print 1
    print("zeta.B:", int(zeta.B))  # Should print 2
    print("zeta.C:", int(zeta.C))  # Should print 3

if __name__ == "__main__":
    main()
```

```cpp
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

```


# Input example
```cpp
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

    struct Nested 
    {
        std::string print(int real)
        {
            return std::to_string(real);
        }
    };

    Nested n;

    zeta z;
};

} // namespace n2::n3

enum class beta : int { A = -1, B, C };

}; // namespace n1

```



# Generator Output example
```
newArgv[1] = another_ex.h
newArgv[2] = --
newArgv[3] = -xc++
newArgv[4] = -std=c++17
newArgv[5] = -I/usr/lib/gcc/x86_64-redhat-linux/14/include
newArgv[6] = -I/usr/include/c++/14
newArgv[7] = -I/usr/include/c++/14/x86_64-redhat-linux
newArgv[8] = -I/usr/include/c++/14/backward
newArgv[9] = -I/usr/local/include
newArgv[10] = -I/usr/include
Source path: another_ex.h
Processing file: ..../python-bindings/another_ex.h
Header: another_ex.h system: <no> (..../python-bindings/another_ex.h)
Header: complex system: <yes> (/usr/include/c++/14/complex)
Header: cstdint system: <yes> (/usr/include/c++/14/cstdint)
Header: string system: <yes> (/usr/include/c++/14/string)
Struct: ComplexWrapper (n1::ComplexWrapper)
    Complex c
Struct: alpha (n1::n2::n3::alpha)
    std::complex<double> a
    std::string b
    Nested n
    zeta z
Struct: zeta (n1::n2::n3::alpha::zeta)
    std::uint16_t A
    std::uint16_t B
    std::uint16_t C
Struct: Nested (n1::n2::n3::alpha::Nested)
    ComplexWrapper cw
Struct: empty (n1::n2::n3::alpha::empty)
Struct: beta (n1::beta)
    int A
    int B
    int C
Function: print (n1::n2::n3::alpha::print)
    Return type: std::string (std::string)
    Parameters:
        int (int) imag (imag)
Function: print (n1::n2::n3::alpha::Nested::print)
    Return type: std::string (std::string)
    Parameters:
        int (int) real (real)
```



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

argv[1] = struct_example_impl.cpp
argv[2] = --
argv[3] = -std=c++14
argv[4] = -I/usr/lib/gcc/x86_64-redhat-linux/14/include
argv[5] = -I/usr/include/c++/14
argv[6] = -I/usr/include/c++/14/x86_64-redhat-linux
argv[7] = -I/usr/include/c++/14/backward
argv[8] = -I/usr/local/include
argv[9] = -I/usr/include
Source path: struct_example_impl.cpp
BeginSourceFileAction
CreateASTConsumer
[ NAMESPACE: test2 ]A
[ NAMESPACE: test2 ]B
[ NAMESPACE: test ]EmptyOuter
[ NAMESPACE: test ]A
B
C
[ NAMESPACE: test ]B
Header: tmp/struct_example_impl.h system: <no> (struct_example_impl.h)
Header: string system: <yes> (/usr/include/c++/14/string)
Struct: test2::A
    int test2::A::a1
    double test2::A::a2
    std::string test2::A::a3
Struct: test2::B
    std::string test2::B::b
Struct: test::EmptyOuter
Struct: test::A
    int test::A::a1
    double test::A::a2
    std::string test::A::a3
    C test::A::c
    EmptyOuter test::A::eo
Struct: test::A::B
Struct: test::A::C
    int test::A::C::c1
    int test::A::C::c2
Struct: test::B
    std::string test::B::b
    A test::B::a
    struct A::B test::B::ab
    struct A::C test::B::ac
Function: processA
    Return type: void
    Parameters:
        const A & 
Function: processB
    Return type: void
    Parameters:
        const B & 
Function: processA
    Return type: void
    Parameters:
        const A & 
Function: processB
    Return type: void
    Parameters:
        const B & 
Function: main
    Return type: int

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


# Programming Guide - Recursive visitor

```cpp
// Get the declaration context for the current declaration
const clang::DeclContext *declContext = declaration->getDeclContext();

// Check for different types of declaration contexts using dynamic_cast or dyn_cast

// 1. Namespace Declaration
if (const auto *namespaceDecl = llvm::dyn_cast<clang::NamespaceDecl>(declContext)) {
    // Handle namespace-specific operations
    llvm::outs() << "Namespace: " << namespaceDecl->getName() << "\n";
}

// 2. Class/Record Declaration
if (const auto *recordDecl = llvm::dyn_cast<clang::CXXRecordDecl>(declContext)) {
    // Handle class/struct-specific operations
    llvm::outs() << "Class/Struct: " << recordDecl->getName() << "\n";
}

// 3. Function Declaration
if (const auto *functionDecl = llvm::dyn_cast<clang::FunctionDecl>(declContext)) {
    // Handle function-specific operations
    llvm::outs() << "Function: " << functionDecl->getName() << "\n";
}

// 4. Translation Unit (Global Scope)
if (const auto *translationUnitDecl = llvm::dyn_cast<clang::TranslationUnitDecl>(declContext)) {
    // Handle global/translation unit scope
    llvm::outs() << "Global Scope\n";
}

// 5. Linkage Specification
if (const auto *linkageSpecDecl = llvm::dyn_cast<clang::LinkageSpecDecl>(declContext)) {
    // Handle extern "C" or extern "C++" contexts
    llvm::outs() << "Linkage Specification Context\n";
}

// 6. Enum Declaration
if (const auto *enumDecl = llvm::dyn_cast<clang::EnumDecl>(declContext)) {
    // Handle enum-specific operations
    llvm::outs() << "Enum: " << enumDecl->getName() << "\n";
}

// 7. Lambda Expression Context
if (const auto *lambdaDecl = llvm::dyn_cast<clang::CXXRecordDecl>(declContext)) {
    if (lambdaDecl->isLambda()) {
        // Handle lambda-specific operations
        llvm::outs() << "Lambda Context\n";
    }
}

// 8. Block Context (Objective-C)
if (const auto *blockDecl = llvm::dyn_cast<clang::BlockDecl>(declContext)) {
    // Handle block-specific operations
    llvm::outs() << "Block Context\n";
}


``
