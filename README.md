# A C++20 CBOR Library with Automatic Reflection

This library is designed with modern C++20 features, with a simple but flexible API, providing full control over memory and protocol customization

It is primarily a library for encoding and decoding Concise Binary Object Representation (CBOR) data. CBOR is a data format designed for small encoded sizes and extensibility without version negotiation. As an information model, CBOR is a superset of JSON, supporting additional data types and custom type definitions via tags 🏷️.

The primary advantage of using a library like this is the ability to define your own data structures and encode/decode them in a way that is both efficient and easy to distribute. All another party needs is to know the tag number and the CDDL (RFC 8610) definition of the object. If using this library on both ends, just the struct definition is enough to encode/decode the data.

The design is inspired by [zpp_bits](https://github.com/eyalz800/zpp_bits) and [bitsery](https://github.com/fraillt/bitsery)

## 🎯 Key Features

- Support for both contiguous and non-contiguous buffers
- Support Zero-copy encoding by joining multiple buffers
- Support Zero-copy decoding using views and spans
- Flexible tag handling for structs and tuples, can be non-invasive
- Support for many (almost arbitrary) containers and nesting
- Uses tl::expected in absence of c++23 std::expected

## 🔧 Quick Start
Here is how you would encode individual CBOR values onto a buffer "data", a type of your choice (e.g it could be a list, deque or pmr::vector)
```cpp
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cassert>
#include <iostream>
#include <vector>

int main() {
    using namespace cbor::tags;

    // Encoding
    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);
    using namespace std::string_view_literals;
    enc(2, 3.14, "Hello, World!"sv); // Note a plain const char* will not work, without a encode overload

    // Decoding
    auto dec = make_decoder(data);
    int  a;
    double b;
    std::string c;
    dec(a, b, c);

    assert(a == 2);
    assert(b == 3.14);
    assert(c == "Hello, World!");

    return 0;
}
```
> [!NOTE]
> CBOR basically requires one to wrap a group into a CBOR array, in order for it to be a single item with known length. This is so that generic parsers can decode tags without knowing their semantic meaning or exact order. It is possible to turn this off in this library, but then it is no longer truly standard cbor, depending on how you interpret the standard. If you set wrap_groups = false, then values in above buffer are 3 single items, and the exact sequence of cbor items is known from the type itself.

Here is how you could formulate a struct with a tag from the first example:
```cpp
struct Tagged {
    static constexpr std::uint64_t cbor_tag = 321;
    int            a;
    double         b;
    std::string    c;
};
```

Here is a manual way to encode the struct, 
```cpp
Tagged a{.a = 2, .b = 3.14, .c = "Hello, World!"};
enc(a.cbor_tag, as_array{3}, a.a, a.b, a.c);
// Or equivalently any of the 2 lines below
// enc(a.cbor_tag, wrap_as_array{a.a, a.b, a.c});
// enc(a);
// Now the buffer contains the tag(321) followed by a single array with 3 elements

```
this is what happens under the hood when you pass the whole item "a".

Here is a larger example of encoding and decoding a struct with all CBOR major types:
```cpp
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cassert>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

using namespace cbor::tags;

/* Below is some example mappings of STL types to cbor major types */
struct AllCborMajorsExample {
    static constexpr std::uint64_t cbor_tag = 3333; //Major type 6 (tag)
    positive                   a0; // Major type 0 (unsigned integer)
    negative                   a1; // Major type 1 (negative integer)
    int                        a;  // Major type 0 or 1 (unsigned or negative integer)
    std::string                b;  // Major type 2 (text string)
    std::vector<std::byte>     c;  // Major type 3 (byte string)
    std::vector<int>           d;  // Major type 4 (array)
    std::map<int, std::string> e;  // Major type 5 (map)
    struct B {
        static_tag<1337> cbor_tag; // Major type 6 (tag)
        bool             a;        // Major type 7 (simple value)
    } f;
    double g;                      // Major type 7 (float)

    // More advanced types
    std::variant<int, std::string, std::vector<int>> h; // Major type 0, 1, 2 or 4 (array can take major type 0 or 1)
    std::unordered_multimap<std::string, std::variant<int, std::map<std::string, double>, std::vector<float>>> i; // Major type 5 (map) ...
    std::optional<std::map<int, std::string>> j; // Major type 5 (map) or 7 (simple value std::nullopt)
};

int main() {
    // Encoding
    auto data = std::vector<std::byte>{};
    auto enc  = make_encoder(data);

    auto a0 = AllCborMajorsExample{
        .a0 = 42,  // +42
        .a1 = 42,  // -42 (implicit conversion to negative)
        .a  = -42, // -42 (integer could be +/-)
        .b  = "Hello, World!",
        .c  = {std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03}},
        .d  = {1, 2, 3, 4, 5},
        .e  = {{1, "one"}, {2, "two"}, {3, "three"}},
        .f  = {.cbor_tag = {}, .a = true},
        .g  = 3.14,
        .h  = "Hello, World!",
        .i  = {{"one", 1}, {"two", std::map<std::string, double>({{"0", 1.0}, {"1", 1.1}})}, {"one", std::vector<float>{0.0f, 1.0f, 2.0f}}},
        .j  = std::nullopt};

    auto result = enc(a0);
    if (!result) {
        std::cerr << "Failed to encode A" << std::endl;
        return 1;
    }

    // Decoding
    auto                 dec = make_decoder(data);
    AllCborMajorsExample a1;
    result = dec(a1);
    if (!result) {
        std::cerr << "Failed to decode A: " << status_message(result.error()) << std::endl;
        return 1;
    }

    assert(a0.a0 == a1.a0);
    return 0;
}
```
> [!NOTE]
> The encoding is basically just a "tuple cast", that a fold expression apply encode(...) to, for each member. The definition of the struct is what sets > the expectation when decoding the data. Any mismatch when decoding will result in a error, e.g invalid_major_type_for_*. An incomplete decode will result in status_code "incomplete". This property is important for understanding the streaming support, which is not yet implemented.

The example below show how cbor tags can be utilized for version handling. There is no explicit version handling in the protocol, instead a tag can represent a new object, which *you* the application developer can, by your definition, decide to be a new version of an object.
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
    auto status = enc(v2::UserProfile{.name = "John Doe", .age = 30, .role = roles::admin});
    if (!status) {
        std::cerr << "Encoding status: " << status_message(status.error()) << std::endl;
        return 1;
    }
    // ...

    // Decoding - supporting multiple versions
    using variant = std::variant<v1::UserProfile, v2::UserProfile>;
    auto    dec = make_decoder(data);
    variant user;
    auto    status = dec(user);
    if (!status) {
        std::cerr << "Decoding status: " << status_message(status.error()) << std::endl;
        return 1;
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
const auto &tuple = to_tuple(A42{{/*42*/}, "John Doe"});

auto enc = make_encoder(...);
std::apply([&enc](const auto &...args) { (enc.encode(args), ...); }, tuple);
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
  GIT_TAG v0.4.1 # or specify a particular commit/tag
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