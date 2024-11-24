# 🚀 Modern C++20 CBOR Library with Static Reflection

This library is designed with modern C++20 features, providing a type-safe, compile-time validated CBOR implementation with special focus on tagged types and static reflection capabilities.

## 🎯 Key Features

> [!TIP]
> - Static visitor pattern for composable encoders/decoders
> - CRTP-based design for zero-overhead abstractions
> - Static reflection via `to_tuple(...)` helper
> - Flexible tag handling for struct and tuples (static, dynamic, and inline)
> - STL container support (including optional and variants)
> - Header-only library

## 🔧 Quick Start

```cpp
// Define a simple tagged structure
struct UserProfile {
    static constexpr std::uint64_t cbor_tag = 140; // Inline tag
    std::string name;
    int64_t age;
};

// Encoding
auto data = std::vector<std::byte>{};
auto enc = make_encoder(data);
enc(UserProfile{"John Doe", 30});

// Decoding
auto dec = make_decoder(data);
UserProfile result;
dec(result);
```

## 🎨 Advanced Usage: Tag Handling

> [!NOTE]
> The library supports three different ways to handle CBOR tags:

### 1. Static Tags
```cpp
struct StaticTagged {
    static_tag<140> cbor_tag;
    std::optional<std::string> data;
};
```

### 2. Dynamic Tags
```cpp
struct DynamicTagged {
    dynamic_tag<uint64_t> cbor_tag;
    std::optional<std::string> data;
};
```

### 3. Inline Tags
```cpp
struct InlineTagged {
    static constexpr std::uint64_t cbor_tag = 140;
    std::optional<std::string> data;
};
```

> [!IMPORTANT]
> As per the extended model, when tagging a struct, you implicitly define a object with it's own CDDL (RFC...).
> This likely has to be handled manually by decoders that doesn't have the header definition and this library at hand.
> Instead one can wrap the struct in an array(wrap_as_array{}), so the tag can be handled in a generic way by all decoders.

## 🔄 Static Reflection

> [!NOTE]
> Until C++26 introduces native reflection, this library provides a powerful alternative using `to_tuple(...)`:

```cpp
struct ComplexType {
    static_tag<140> cbor_tag;
    int a;
    std::string b;
    std::vector<double> c;
};

// Automatic serialization/deserialization
auto data = std::vector<std::byte>{};
auto enc = make_encoder(data);
enc(ComplexType{{}, 42, "Hello", {1.0, 2.0}});
```

## 🛠️ Requirements

- C++20 compatible compiler (gcc 12+)
- CMake 3.20+

## 📦 Installation

Standard cmake:

```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
  cbor_tags
  GIT_REPOSITORY https://github.com/jkammerland/cbor_tags.git
  GIT_TAG v0.1.0 # or specify a particular commit/tag
)

FetchContent_MakeAvailable(cbor_tags)

add_executable(my_target main.cpp)
target_link_libraries(my_target PRIVATE cbor_tags)
```

System-wide install:

```bash
git clone https://github.com/yourusername/cbor_tags
cd cbor_tags
mkdir build && cd build
cmake ..
make install
```

## 💡 CMake Integration

```cmake
find_package(cbor_tags REQUIRED)
target_link_libraries(your_target PRIVATE cbor_tags::cbor_tags)
```

> [!WARNING]
> This library requires C++20 features. Ensure your compiler supports them before use.

## 📚 Documentation

For more examples and detailed documentation, visit our [Wiki](link-to-wiki).

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---

> [!TIP]
> For best performance, compile with optimization flags enabled (`-O3` for GCC/Clang).