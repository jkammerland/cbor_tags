import pybinding1


def main():
    print("Hello from Python!")
    a = pybinding1.A()
    a.a1 = 1
    a.a2 = 3.14
    a.a3 = "hello world!"
    pybinding1.processA(a)
    

if __name__ == "__main__":
    main()
    