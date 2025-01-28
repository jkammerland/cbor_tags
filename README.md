# A C++20 CBOR Library with Automatic Reflection

This library is designed with modern C++20 features, with a simple but flexible API, providing full control over memory and protocol customization

It is primarily a library for encoding and decoding Concise Binary Object Representation (CBOR) data. CBOR is a data format designed for small encoded sizes and extensibility without version negotiation. As an information model, CBOR is a superset of JSON, supporting additional data types and custom type definitions via tags 🏷️.

The primary advantage of using a library like this is the ability to define your own data structures and encode/decode them in a way that is both efficient and easy to distribute. All another party needs is to know the tag number and the CDDL (RFC 8610) definition of the object. If using this library on both ends, just the struct definition is enough to encode/decode the data.

See standard specifications for more information:
- CDDL [RFC8610](https://datatracker.ietf.org/doc/html/rfc8610) support for defining custom data structures
- CBOR [RFC8949](https://datatracker.ietf.org/doc/html/rfc8949) 

The design is inspired by [zpp_bits](https://github.com/eyalz800/zpp_bits) and [bitsery](https://github.com/fraillt/bitsery)

## 🎯 Key Features

- Support for both contiguous and non-contiguous buffers
- Support Zero-copy encoding by joining multiple buffers
- Support Zero-copy decoding using views and spans
- Flexible tag handling for structs and tuples, can be non-invasive
- Support for many (almost arbitrary) containers and nesting
- Configurable API, defaults to tl::expected<void, cbor::tags::status_code> in absence of c++23 std::expected (has a almost 1 to 1 mapping)

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
    // make a data buffer - holding the encoded data
    auto data = std::vector<std::byte>{};

    // Encoding
    auto enc  = make_encoder(data);
    using namespace std::string_view_literals;
    enc(2, 3.14, "Hello, World!"sv); // Note a plain const char* will not work, without a encode overload

    // Emulate transfer of data buffer
    auto trasmitted_data = data;

    // Decoding
    auto dec = make_decoder(trasmitted_data);
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
enc(a.cbor_tag, as_array{3}, a.a, a.b, a.c); // same as enc(a);
// Also equivalent to:
// enc(a.cbor_tag, wrap_as_array{a.a, a.b, a.c});
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
    // Declare data
    auto data = std::vector<std::byte>{};

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

    // Encoding
    auto enc  = make_encoder(data);
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
    assert(a0.a1 == a1.a1);
    assert(a0.a == a1.a);
    assert(a0.b == a1.b);
    assert(a0.c == a1.c);
    assert(a0.d == a1.d);
    assert(a0.e == a1.e);
    assert(a0.f.a == a1.f.a);
    assert(a0.g == a1.g);
    assert(a0.h == a1.h);
    assert(a0.i == a1.i);
    assert(a0.j == a1.j);
    return 0;
}
```
> [!NOTE]
> The encoding is basically just a "tuple cast", that a fold expression apply encode(...) to, for each member. The definition of the struct is what sets the expectation when decoding the data. Any mismatch when decoding will result in a error, e.g invalid_major_type_for_*. An incomplete decode will result in status_code "incomplete". This property is important for understanding the streaming support, which is not yet implemented.

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

In some cases where more flexability is needed, we can just parse any tag and then switch on it like this:

```cpp
// Example API structures
struct Api1 {
    int a;
    int b;
};

struct Api2 {
    std::string a;
    std::string b;
};

int main() {
    using namespace cbor::tags;
    auto data = std::vector<std::byte>{};
    auto enc = make_encoder(data);

    // Encode Api1 with a tag of 0x10 - note that the tag does not have to be part of the struct
    enc(make_tag_pair(0x10, Api1{.a = 42, .b = 43}));

    // Encode a binary string in the middle of the buffer [the buffer itself]
    enc(std::span{data});

    // Encode Api2 with a tag of 0x20
    enc(make_tag_pair(0x20, Api2{"hello", "world"}));

    // Decoding
    auto dec = make_decoder(data);
    std::variant<std::vector<std::byte>, as_tag_any> value;

    auto visitor = [&dec](auto&& value) {
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, as_tag_any>) {
            if (value.tag == 0x10) {
                Api1 a{};
                if (dec(a)) {
                    std::cout << "Api1: a=" << a.a << ", b=" << a.b << "\n";
                }
            } else if (value.tag == 0x20) {
                Api2 a{};
                if (dec(a)) {
                    std::cout << "Api2: a=" << a.a << ", b=" << a.b << "\n";
                }
            } else {
                std::cout << "Unknown tag: " << value.tag << "\n";
            }
        } else {
            std::cout << "Binary data received\n";
        }
    };

    // Decode three items
    if (dec(value)) {
        std::visit(visitor, value);
    }

    if (dec(value)) {
        std::visit(visitor, value);
    }

    if (dec(value)) {
        std::visit(visitor, value);
    }

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

> [!IMPORTANT]
> The library supports multiple different ways to handle CBOR tags:

### 1. Inline Tags
If the struct is used as a cbor object, then it makes sense to tag it directly in the struct definition:
```cpp
struct InlineTagged {
    static constexpr std::uint64_t cbor_tag = 140;
    std::optional<std::string> data;
};
```

### 2. Literal Tags
Inline tagging can be a bit invasive if not strictly cbor, you don't "own" it or want to modify the struct. In this case, you can use a literal tag, which is a constexpr variable of type static_tag<N>:
```cpp
using namespace cbor::tags::literals;
struct A { std::string a; };
auto &&tagged = make_tag_pair(140_tag, A{"Hello, World!"});
```


### 3. Static Tags
This is the same as inline tags, but the tag value is a static member of the struct static_tag<N>.
It is also the same as defining the literal 140_tag. The primary purpose is so that you can tag a struct without modifying it, by making a pair with the tag and the struct. But you can also inline it:
```cpp
struct StaticTagged {
    static_tag<140> cbor_tag;
    std::optional<std::string> data;
};
```

### 4. Dynamic Tags
Dynamic can be set at runtime, will return error if it does not match tag on the buffer when decoding.
Note that this means the tag byte(s) can not be optimized away in the resulting structure.
```cpp
struct DynamicTagged {
    dynamic_tag<uint64_t> cbor_tag;
    std::optional<std::string> data;
};
```

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

## 🏷️ Annotating CBOR Buffers
You can use "annotate" from cbor_tags/extensions/cbor_cddl.h to inspect and visualize CBOR data:

For example, here is a cbo web token without diagnostic notation:
```
CBOR Web Token (CWT): d28443a10126a104524173796d6d657472696345434453413235365850a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b77037818636f61703a2f2f6c696768742e6578616d706c652e636f6d041a5612aeb0051a5610d9f0061a5610d9f007420b7158405427c1ff28d23fbad1f29c4c7c6a555e601d6fa29f9179bc3d7438bacaca5acd08c8d4d4f96131680c429a01f85951ecee743a52b9b63632c57209120e1c9e30
Annotation: 
d2
   84
      43
         a10126
      a1
         04
         52
            4173796d6d65747269634543445341323536
      58 50
         a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b77037818636f61703a2f2f6c696768742e6578616d706c652e636f6d041a5612aeb0051a5610d9f0061a5610d9f007420b71
      58 40
         5427c1ff28d23fbad1f29c4c7c6a555e601d6fa29f9179bc3d7438bacaca5acd08c8d4d4f96131680c429a01f85951ecee743a52b9b63632c57209120e1c9e30
```

```
CBOR: 8b01206c48656c6c6f20776f726c64214461626364a301636f6e65026374776f03657468726565d88c82d88e821a000f42407818616161616161616161616161616161616161616161616161182af94247fa4048f5c3fb40091eb851eb851ff5f6
Annotation: 
8b
   01
   20
   6c
      48656c6c6f20776f726c6421
   44
      61626364
   a3
      01
      63
         6f6e65
      02
      63
         74776f
      03
      65
         7468726565
   d8 8c
      82
         d8 8e
            82
               1a 000f4240
               78 18
                  616161616161616161616161616161616161616161616161
            18 2a
         f9 4247
         fa 4048f5c3
      fb 40091eb851eb851f
   f5
   f6
```

CDDL example:
``` 
struct B {
    static constexpr std::uint64_t cbor_tag = 140;
    std::vector<std::byte>         a;
    std::map<int, std::string>     b;
};

struct C {
    static_tag<141>  cbor_tag;
    int              a;
    std::string      b;
    std::optional<B> c;
};
struct A {
        uint32_t                       a1;
        negative                       aminus;
        int                            a;
        double                         b;
        float                          c;
        bool                           d;
        std::string                    e;
        std::vector<std::byte>         f;
        std::map<int, std::string>     g;
        std::variant<int, std::string> h;
        std::optional<int>             i;
        B                              j;
        C                              k;
    };

CDDL:
A = (uint, nint, int, float64, float32, bool, tstr, bstr, map, int / tstr, int / null, B, C)
C = #6.141([int, tstr, B / null])
B = #6.140([bstr, map])
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
  GIT_TAG v0.5.1 # or specify a particular commit/tag
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
- CDDL [RFC8610](https://datatracker.ietf.org/doc/html/rfc8610) support for defining custom data structures

For more examples and detailed documentation, visit our [Wiki](link-to-wiki).

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---