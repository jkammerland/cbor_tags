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
    
    # Print enum values
    print("zeta.A:", int(zeta.A))  # Should print 1
    print("zeta.B:", int(zeta.B))  # Should print 2
    print("zeta.C:", int(zeta.C))  # Should print 3

if __name__ == "__main__":
    main()