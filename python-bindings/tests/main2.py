import pybinding2

def main():
    print("Hello from Python!")
    a = pybinding2.alpha()
    a.b = "Hello from C++!"
    output = a.print(2)
    print(output)
    
    # Using the nested enum zeta
    z = pybinding2.zeta.A
    print(f"zeta.A = {int(z)}")
    
    a.z = pybinding2.zeta.B
    print(f"a.z = {a.z}")
    print(f"a.z as int = {int(a.z)}")
    
    # Print individual enum values
    print("\nzeta values:")
    print(f"A = {int(pybinding2.zeta.A)}")
    print(f"B = {int(pybinding2.zeta.B)}")
    print(f"C = {int(pybinding2.zeta.C)}")
    
    # Regular enum beta
    b = pybinding2.beta.A
    print(f"\nbeta.A = {int(b)}")
    
    b = pybinding2.beta.B
    print(f"beta.B = {int(b)}")
    
    b = pybinding2.beta.C
    print(f"beta.C = {int(b)}")

if __name__ == "__main__":
    main()