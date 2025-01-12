# A C++20 CBOR Library with Automatic Reflection

This library is designed with modern C++20 features, providing a simple but flexible API, with full control over memory and protocol customization. 

It is primarily a library for encoding and decoding Concise Binary Object Representation (CBOR) data. CBOR is a data format designed for small encoded sizes and extensibility without version negotiation. As an information model, CBOR is a superset of JSON, supporting additional data types and custom type definitions via tags 🏷️.

The design is inspired by [zpp_bits](https://github.com/eyalz800/zpp_bits) and [bitsery](https://github.com/fraillt/bitsery)

## 🎯 Key Features

- Support for both contiguous and non-contiguous buffers
- Support Zero-copy encoding by joining multiple buffers
- Support Zero-copy decoding using views and spans
- Flexible tag handling for structs and tuples, non-invasive
- Support for arbitrary containers (concept-based)
- Header-only library, modules TODO:
- Uses tl::expected in absence of c++23 std::expected

## 🔧 Quick Start

```cpp
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cassert>
#include <iostream>
#include <variant>
#include <string>
#include <vector>

enum class roles : std::uint8_t {
    admin,
    user,
    guest
};

namespace v1 {
struct UserProfile {
    static constexpr std::uint64_t cbor_tag = 140; // Inline tag
    std::string name;
    int64_t age;
};
}

namespace v2 {
struct UserProfile {
    static constexpr std::uint64_t cbor_tag = 141; // Inline tag
    std::string name;
    int64_t age;
    roles role;
};
}

int main() {
    using namespace cbor::tags;

    // Encoding
    auto data          = std::vector<std::byte>{};
    auto enc           = make_encoder(data);
    auto result_encode = enc(v2::UserProfile{.name = "John Doe", .age = 30, .role = roles::admin});
    if (!result_encode) {
        std::cerr << "Encoding status: " << status_to_message(result_encode.error()) << std::endl;
    }
    // ...

    // Decoding - supporting multiple versions
    using variant = std::variant<v1::UserProfile, v2::UserProfile>;
    auto    dec = make_decoder(data);
    variant user;
    auto    result_decode = dec(user);
    if (!result_decode) {
        std::cerr << "Decoding status: " << status_to_message(result_decode.error()) << std::endl;
    }
    // should now hold a v2::UserProfile
    // ...

    assert(std::holds_alternative<v2::UserProfile>(user));
    auto &user_v2 = std::get<v2::UserProfile>(user);
    std::cout << "Name: " << user_v2.name << std::endl;
    std::cout << "Age:  " << user_v2.age << std::endl;
    std::cout << "Role: " << static_cast<int>(user_v2.role) << std::endl;

    return 0;
}
```

## ✨ Advanced Features

- std::variant support, to allow multiple types to be accepted when seen on buffer (e.g tagged types representing a versioned object)
- options for encoder/decoder, such as index tracking for resuming decoding
- TODO: streaming support, via api adapter using the return value of an incomplete decode 
- TODO: CDDL support, for defining custom data structures
- TODO: unique_ptr support
- TODO: shared_ptr support

## 🎨 Custom Tag Handling

> [!NOTE]
> The library supports three different ways to handle CBOR tags:

### 1. Inline Tags
```cpp
struct InlineTagged {
    static constexpr std::uint64_t cbor_tag = 140;
    std::optional<std::string> data;
};
```

### 2. Static Tags
```cpp
struct StaticTagged {
    static_tag<140> cbor_tag;
    std::optional<std::string> data;
};
```

### 3. Dynamic Tags
Dynamic can be set at runtime, will return error if it does not match tag on buffer when decoding.
Note that this means the tag byte(s) can not easily be optimized away in the resulting structure.
```cpp
struct DynamicTagged {
    dynamic_tag<uint64_t> cbor_tag;
    std::optional<std::string> data;
};
```

> [!IMPORTANT]
> As per the extended model, when tagging a struct, you implicitly define an object with its own CDDL (RFC 8610) structure.
> This likely has to be handled manually by decoders that don't have the header definition and this library at hand.
> Instead one can wrap the struct in an array using `wrap_as_array{}`, so the tag can be handled in a generic way by all decoders.

## 🔄 Automatic Reflection

> [!NOTE]
> Until C++26 (or later) introduces native reflection, or auto [...ts] = X{}, this library provides an alternative compiler trick using `to_tuple(...)`:

```cpp
template <size_t N> struct A0 {
    static_tag<N> cbor_tag;
    std::string name;
};

using A42 = A0<42>;
auto && [tag, name] = to_tuple(A42{{/*42*/}, "John Doe"});
//...

```

## 🛠️ Requirements

- Any C++20 compatible compiler (gcc 12+, clang 14+, msvc (builds but broken))
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
  GIT_TAG v0.2.1 # or specify a particular commit/tag
)

FetchContent_MakeAvailable(cbor_tags)

add_executable(my_target main.cpp)
target_link_libraries(my_target PRIVATE cbor_tags)
```

System-wide install:

```bash
git clone https://github.com/jkammerland/cbor_tags
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

## ⚙️ C++20 Requirements Note

> [!NOTE]
> This library requires C++20 features. However, you can isolate these requirements using the PIMPL idiom - wrapping this library and exposing an API compatible with your target C++ version.

## 📚 Documentation

There are many types of cbor objects defined, the major types are:

| Major Type | Meaning                 | Content               |
|------------|-------------------------|-----------------------|
| 0          | unsigned integer N      | -                     |
| 1          | negative integer -1-N   | -                     |
| 2          | byte string N bytes     | -                     |
| 3          | text string N bytes     | UTF-8 text            |
| 4          | array N data items      | elements              |
| 5          | map 2N data items       | key/value pairs       |
| 6          | tag of number N         | 1 data item           |
| 7          | simple/float            | -                     |


The name cbor_tags refers to the focus on handling tagged types(6) in a user friendly way. 

"A tagged data item ("tag") whose tag number, an integer in the range 0..2^64-1 inclusive, is the argument and whose enclosed data item (tag content) is the single encoded data item that follows the head. See [RFC8949#tags](https://www.rfc-editor.org/rfc/rfc8949.html#tags)"

**This means that if you want to make an object for public use, you can define the exact serialization of that object and tag it.**

Please see the public online database of [tags](https://www.iana.org/assignments/cbor-tags/cbor-tags.xhtml), where the tags are grouped as follows:

| Tag Range                   | How to Register         |
|-----------------------------|-------------------------|
| 0-23                        | Standards Action        |
| 24-32767                    | Specification Required  |
| 32768-18446744073709551615  | First Come First Served |


- Supports encoding and decoding of various CBOR data types according to [RFC8949](https://datatracker.ietf.org/doc/html/rfc8949) 
- Streaming TODO:
- CDDL TODO:

For more examples and detailed documentation, visit our [Wiki](link-to-wiki).

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---