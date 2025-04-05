# A C++20 CBOR Library with Automatic Reflection

[![CI](https://github.com/jkammerland/cbor_tags/actions/workflows/macos.yml/badge.svg)](https://github.com/jkammerland/cbor_tags/actions/workflows/macos.yml)
[![CI](https://github.com/jkammerland/cbor_tags/actions/workflows/ubuntu.yml/badge.svg)](https://github.com/jkammerland/cbor_tags/actions/workflows/ubuntu.yml)
[![CI](https://github.com/jkammerland/cbor_tags/actions/workflows/windows.yml/badge.svg)](https://github.com/jkammerland/cbor_tags/actions/workflows/windows.yml)

This is a library for encoding and decoding Concise Binary Object Representation (CBOR) data. CBOR is a data format designed for small encoded sizes and extensibility without version negotiation. As an information model, CBOR is a superset of JSON, supporting additional data types and custom type definitions via tags 🏷️. See [xkcd/927](https://xkcd.com/927/).

The primary advantage of using this library is the ability to define your own data structures and encode/decode them in a way that is both efficient and easy to distribute. All another party needs is to know the tag number and the Concise Data Definition of the object. If using this library on both ends, just the struct definition is enough to encode/decode the data.

See standard specifications for more information:
- CDDL [RFC8610](https://datatracker.ietf.org/doc/html/rfc8610) (Concise Data Definition Language)
- CBOR [RFC8949](https://datatracker.ietf.org/doc/html/rfc8949) (Concise Binary Object Representation)

The library design is inspired by [zpp_bits](https://github.com/eyalz800/zpp_bits) and [bitsery](https://github.com/fraillt/bitsery), but uses the CBOR standard as binary format instead of a custom one. It also provides intuitive error handling and gives full control over memory layouts and buffer management. By supporting all standard containers and abstractions, the API for handling the data before/after encoding/decoding should be familiar without requiring detailed knowledge of the CBOR format.


# Index

- [🎯 Key Features](#-key-features)
- [🔧 Quick Start](#-quick-start)
  - [Basic Encoding/Decoding Example](#basic-encodingdecoding-example)
  - [Tagged Struct Example](#tagged-struct-example)
  - [Advanced Type Support](#advanced-type-support)
  - [Version Handling with Variants](#version-handling-with-variants)
  - [Manual Tag Parsing](#manual-tag-parsing)
- [✨ Advanced Features](#-advanced-features)
- [🎨 Custom Tag Handling](#-custom-tag-handling)
- [🔄 Automatic Reflection](#-automatic-reflection)
- [🏷️ Annotating buffers and Diagnostic notation](#️-annotating-cbor-buffers)
- [🤝 CDDL Schema Generation](#-cddl-schema-generation)
- [✅ Requirements](#-requirements)
- [📦 Installation](#-installation)
- [💡 CMake Integration](#-cmake-integration)
- [📚 Documentation](#-documentation)
  - [IANA Tag Registry](#iana-tag-registry)
- [📄 License](#-license)

---

## 🎯 Key Features

- Support for both contiguous and non-contiguous buffers.
- Ranges support.
- Zero-copy encoding by joining multiple buffers.
- Zero-copy decoding using views and spans.
- Flexible tag handling for structs and tuples, can be completely non-invasive on your code.
- Support for many (almost arbitrary) containers and nesting.
- noexcept API (encode/decode), return value defaults to `tl::expected<void, status_code>` in the absence of C++23's `std::expected` (with an almost 1-to-1 mapping).
- CDDL support for schema and custom data definitions.

## 🔧 Quick Start
### Basic Encoding/Decoding Example
Basic example of encoding and decoding a single cbor items:
```cpp
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cassert>
#include <iostream>
#include <vector>

using namespace cbor::tags;
using namespace std::string_view_literals;

int main() {
    // Create a data buffer to hold the encoded data (could be std::deque, std::pmr::vector, std::array, ...)
    auto data = std::vector<std::byte>{};

    // Encoding (sender side)
    auto enc  = make_encoder(data);
    if(!enc(2, 3.14, "Hello, World!"sv)){
        return 1;
    }

    // Emulate transfer of data buffer
    auto trasmitted_data = data;

    // Declare variables to hold the decoded data (receiver side)
    int  a;
    double b;
    std::string c;

    // Decoding
    auto dec = make_decoder(trasmitted_data);
    if(!dec(a, b, c)){
        return 1;
    }

    assert(a == 2);
    assert(b == 3.14);
    assert(c == "Hello, World!");

    return 0;
}
```
> [!IMPORTANT]
> These values are not grouped in any way. For that they would have to be enclosed in a array, map or binary string. If it is a special sequence of items, you can define a tag for it and share that definition with the recipient(s).

### Tagged Struct Example
Here is how you could formulate a struct with a tag from the first example:
```cpp
struct Tagged {
    static constexpr std::uint64_t cbor_tag = 321;
    int            a;
    double         b;
    std::string    c;
};

Tagged tagged{.a = 2, .b = 3.14, .c = "Hello, World!"}
enc(tagged);
```

> [!NOTE]
> The definition of a tag is a CBOR major type 6 encoded uint, with a concise data definition format of #6.321(any). This allows generic parsers to decode tags without knowing their semantic meaning or the exact order of internal items. It also means the struct implicitly define a cbor array of exact types following the tag, i.e #6.321([int, float64, tstr]). See tuning options for more details.

Equivalent to manually encoding the struct in the following example:
```cpp
Tagged tagged{.a = 2, .b = 3.14, .c = "Hello, World!"};
enc(tagged.cbor_tag, as_array{3}, tagged.a, tagged.b, tagged.c); // same as enc(a);
// Also equivalent to:
// enc(a.cbor_tag, wrap_as_array{a.a, a.b, a.c});
// Now the buffer contains the tag(321) followed by a single array with 3 elements

```

### Advanced Type Support
This can be taken further to any number of members or nesting, e.g a struct with all CBOR major types (and more):
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
        static_tag<123123> cbor_tag; // Major type 6 (tag)
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

    auto obj1 = AllCborMajorsExample{
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
    auto result = enc(obj1);
    if (!result) {
        std::cerr << "Failed to encode A" << status_message(result.error()) << std::endl;
        return 1;
    }

    // Decoding
    auto                 dec = make_decoder(data);
    AllCborMajorsExample obj2;
    result = dec(obj2);
    if (!result) {
        std::cerr << "Failed to decode A: " << status_message(result.error()) << std::endl;
        return 1;
    }

    assert(obj1.a0 == obj2.a0);
    assert(obj1.a1 == obj2.a1);
    assert(obj1.a == obj2.a);
    assert(obj1.b == obj2.b);
    assert(obj1.c == obj2.c);
    assert(obj1.d == obj2.d);
    assert(obj1.e == obj2.e);
    assert(obj1.f.a == obj2.f.a);
    assert(obj1.g == obj2.g);
    assert(obj1.h == obj2.h);
    assert(obj1.i == obj2.i);
    assert(obj1.j == obj2.j);
    return 0;
}
```
> [!NOTE]
> The encoding is basically just a "tuple cast", that a fold expression apply encode(...) to, for each member. The definition of the struct is what sets the expectation when decoding the data. Any mismatch when decoding will result in a status_code, i.e result.error(). An incomplete decode will result in status_code "incomplete". This property is important for understanding the streaming support, though streaming API is still incomplete.

## Working with Private Class Members or explicit overloading
Should the need arise for overloading, or encoding private members, you have two options. The first is to use the `Access` friend class as shown in the example above. This will allow you to access private members of your class for encoding/decoding purposes.

```cpp
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include <vector>
#include <string>
#include <variant>

using namespace cbor::tags;

class PrivateDataClass {
public:
    PrivateDataClass() = default;
    explicit PrivateDataClass(int id, std::string name) 
        : id_(id), name_(std::move(name)) {}
    
    bool operator==(const PrivateDataClass& other) const = default;
    
private:
    int id_ = 0;
    std::string name_;

    /* Optional, and IS AUTOMATICALLY USED IF DEFINED
       This is required for because of some details in std::variant handling with classes
    */
    static_tag<12345> cbor_tag; 
    
    // Method 1: Use member functions for encoding/decoding
    friend cbor::tags::Access;  // Grant access to the library
    
    // You need one or both of these functions, depending on your needs
    // Remember that if this is an object, wrap it in a array or map, as 
    // single items is most likely not what you want. 
    // But if there is only 1 item you don't need any wrapping of course.

    template <typename Encoder>
    constexpr auto encode(Encoder& enc) const {
        return enc(wrap_as_array{id_, name_});  // Encode as array with 2 elements
    }
    
    template <typename Decoder>
    constexpr auto decode(Decoder& dec) {
        return dec(wrap_as_array{id_, name_});  // Decode as array with 2 elements
    }
};

int main() {
    // Create an object with private data
    PrivateDataClass obj{123, "Private Data"};
    
    // Encode the object
    std::vector<uint8_t> buffer;
    auto enc = make_encoder(buffer);
    [[maybe_unused]] _ = enc(obj);
    
    // Decode into a new object
    auto dec = make_decoder(buffer);
    PrivateDataClass decoded_obj;
    [[maybe_unused]] _ = dec(decoded_obj);
    
    // Objects should be equal
    assert(obj == decoded_obj);
    
    return 0;
}
```

Method 2 is to use an overload of encode or decode as free functions. This approach is useful when you cannot or do not want to modify the original class:

```cpp
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include <vector>
#include <string>

using namespace cbor::tags;

// A class we can't modify (perhaps from a third-party library)
class ExternalClass {
public:
    ExternalClass() = default;
    ExternalClass(int id, std::string name) : id_(id), name_(std::move(name)) {}
    
    bool operator==(const ExternalClass& other) const = default;
    
    // Getters for accessing private data
    int getId() const { return id_; }
    const std::string& getName() const { return name_; }
    
    // Setters for modifying private data
    void setId(int id) { id_ = id; }
    void setName(const std::string& name) { name_ = name; }
    
private:
    int id_ = 0;
    std::string name_;
};

// Method 2: Define free functions for encoding and decoding
// This is used when you cannot modify the class itself

// Tag function (optional) - defines a tag for this type when used in a variant
constexpr auto cbor_tag(const ExternalClass&) { return static_tag<54321>{}; }

// Encode function - converts the object to CBOR
template <typename Encoder>
constexpr auto encode(Encoder& enc, const ExternalClass& obj) {
    // Access the private data through getters
    return enc(wrap_as_array{obj.getId(), obj.getName()});
}

// Decode function - reconstructs the object from CBOR
template <typename Decoder>
constexpr auto decode(Decoder& dec, ExternalClass&& /*Must use "&&" to match free function signature, and to allow temporary objects */ obj) {
    // Temporary variables to hold the decoded values
    int id;
    std::string name;
    
    // Decode into temporaries
    auto result = dec(wrap_as_array{id, name});
    
    // If successful, update the object using setters
    if (result) {
        obj.setId(id);
        obj.setName(name);
    }
    
    return result;
}

int main() {
    // Create an object
    ExternalClass obj{456, "External Data"};
    
    // Encode the object
    std::vector<uint8_t> buffer;
    auto enc = make_encoder(buffer);
    auto result = enc(obj);
    
    if (!result) {
        std::cerr << "Encoding failed: " << status_message(result.error()) << std::endl;
        return 1;
    }
    
    // Decode into a new object
    auto dec = make_decoder(buffer);
    ExternalClass decoded_obj;
    result = dec(decoded_obj);
    
    if (!result) {
        std::cerr << "Decoding failed: " << status_message(result.error()) << std::endl;
        return 1;
    }
    
    // Objects should be equal
    assert(obj == decoded_obj);
    
    return 0;
}
```

### Version Handling with Variants
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
    variant user;
    auto    dec = make_decoder(data);
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

### Manual Tag Parsing
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

    // Encode a 0 length binary string in the middle of the buffer
    enc(std::vector<std::byte>{});

    // Encode Api2 with a tag of 0x20
    enc(make_tag_pair(0x20, Api2{"hello", "world"})); 

    // Decoding - accept bstr and any tagged value
    auto dec = make_decoder(data);
    std::variant<std::vector<std::byte>, as_tag_any> value;

    // Define a visitor for our variant type
    auto visitor = [&dec](auto&& value) {
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, as_tag_any>) {
            if (value.tag == 0x10) {
                Api1 a;
                if (dec(a)) {
                    std::cout << "Api1: a=" << a.a << ", b=" << a.b << "\n";
                }
            } else if (value.tag == 0x20) {
                Api2 a;
                if (dec(a)) {
                    std::cout << "Api2: a=" << a.a << ", b=" << a.b << "\n";
                }
            } else {
                std::cout << "Unknown tag: " << value.tag << "\n";
            }
        } else {
            // Since it was not a tag, it must be a bstr, otherwise the decoding would return the unexpected type.
            std::cout << "Binary data received (aka bstr)\n";
        }
    };

    // Decode the three items
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

## ✨ WIP Features

- Done: `std::variant` support, allowing multiple types to be accepted when seen on the buffer (e.g., tagged types representing a versioned object).
- WIP: Complete ranges support
- TODO: Coroutine support for decoding and encoding, more convenient api wrapper when streaming 
- TODO: Options for encoder/decoder, such as (un)expected type tuning
- TODO: Performance tuning options, such as disabling some checks and non-standard encodings.
- TODO: `unique_ptr` support.
- TODO: `shared_ptr` support.

## 🎨 Custom Tag Handling

> [!IMPORTANT]
> The library supports multiple different ways to handle CBOR tags:

### 1. Inline Tags
If the struct is used as a CBOR object, it makes sense to tag it directly in the struct definition:
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
 Until C++26 (or later) introduces native reflection, or `auto [...ts] = X{}`, this library provides an alternative compiler trick using `to_tuple(...)`:

```cpp
struct A42 {
    static_tag<42> cbor_tag;
    std::string    name;
};

const auto  a42   = A42{{/*42*/}, "John Doe"};
const auto &tuple = to_tuple(a42);

std::vector<uint8_t> buffer;
auto                 enc = make_encoder(buffer);
std::apply([&enc](const auto &...args) { (enc.encode(args), ...); }, tuple);
//...

```
This is not necessary todo manually, as the operator() of the de/encoder will do this for you, while stopping at the first error. The supported ranges are configured with the cmake option `CBOR_TAGS_REFLECTION_RANGES`, which defaults to "1:24". This means a struct can at maximum have 24 members, but it can handle any number of nested structs, as long as they are within the max member requirement too. The format can take multiple space separated ranges, e.g. "1:24 30:50 1000:1000", just make sure it matches your usage. Any changes to this option will trigger a regeneration the header automatically, for cmake targets that depend on cbor_tags. The tool can be run separately if not using cmake in your build process.

## 🏷️ Annotating CBOR Buffers
You can use `buffer_annotate` and `buffer_diagnostic` from `cbor_tags/extensions/cbor_visualization.h` to inspect and visualize CBOR data:

See code examples here:
```cpp
// Data vector of a CWT token
std::vector<std::byte> data =
    to_bytes("d28443a10126a104524173796d6d657472696345434453413235365850a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b770"
                "37818636f61703a2f2f6c696768742e6578616d706c652e636f6d041a5612aeb0051a5610d9f0061a5610d9f007420b7158405427c1ff28d23fbad1f2"
                "9c4c7c6a555e601d6fa29f9179bc3d7438bacaca5acd08c8d4d4f96131680c429a01f85951ecee743a52b9b63632c57209120e1c9e30");

// Annotate the data vector
fmt::memory_buffer buffer;
buffer_annotate(data, buffer);
fmt::format_to(std::back_inserter(buffer), "\n --- \n");

// Diagnostic notation of the data vector
buffer_diagnostic(data, buffer, {});
fmt::format_to(std::back_inserter(buffer), "\n --- \n");

// Take the payload map (unwrapping the 3rd bstr element) and make it into diagnostic notation too
data = to_bytes("a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b77037818636f61703a2f2f6c696768742e6578616d706c652e6"
                "36f6d041a5612aeb0051a5610d9f0061a5610d9f007420b71");

buffer_diagnostic(data, buffer, {});
fmt::print("\n{}\n", fmt::to_string(buffer));
```
Should output:
```
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

 --- 
[
18([
  h'a10126',
  {
    4: h'4173796d6d65747269634543445341323536'
  },
  h'a70175636f61703a2f2f61732e6578616d706c652e636f6d02656572696b77037818636f61703a2f2f6c696768742e6578616d706c652e636f6d041a5612aeb0051a5610d9f0061a5610d9f007420b71',
  h'5427c1ff28d23fbad1f29c4c7c6a555e601d6fa29f9179bc3d7438bacaca5acd08c8d4d4f96131680c429a01f85951ecee743a52b9b63632c57209120e1c9e30'
])
]
 --- 
[
{
  1: "coap://as.example.com",
  2: "erikw",
  3: "coap://light.example.com",
  4: 1444064944,
  5: 1443944944,
  6: 1443944944,
  7: h'0b71'
}
]
```

## 🤝 CDDL Schema Generation
For Concise Data Definitions schemas you can use the `cddl_schema_to` method, e.g by applying on a struct "A":
```cpp
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

fmt::memory_buffer buffer;
cddl_schema_to<A>(buffer, {.row_options = {.format_by_rows = false}});
fmt::print("Concise Data Definition: \n{}\n", fmt::to_string(buffer));
```

Should output:
```
A = (uint, nint, int, float64, float32, bool, tstr, bstr, map, int / tstr, int / null, B, C)
C = #6.141([int, tstr, B / null])
B = #6.140([bstr, map])
```
See the docs for more info.


## ✅ Requirements

- tl::expected (required, if not using c++23 std::expected)
- fmt (optional, but required for cddl)
- nameof (optional, but required for cddl)
- C++20 compatible compiler, tested with (GCC 12+, Clang 15+, Clang-CL 15+, MSVC-latest, AppleClang 15+).
- CMake 3.20+.

## 📦 Installation

Standard cmake:

```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
  cbor_tags
  GIT_REPOSITORY https://github.com/jkammerland/cbor_tags.git
  GIT_TAG v0.9.0 # or specify a particular commit/tag
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
> This library requires C++20 features. However, you can isolate that requirement by wrapping this library and exposing an API compatible with your target C++ version.

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

The library name cbor_tags refers to the focus on handling tagged types(6) in a user friendly way. 

"A tagged data item ("tag") whose tag number, an integer in the range 0..2^64-1 inclusive, is the argument and whose enclosed data item (tag content) is the single encoded data item that follows the head. See [RFC8949#tags](https://www.rfc-editor.org/rfc/rfc8949.html#tags)"

**This means that if you want to make an object for public use, you can define the exact serialization of that object and tag it.**

### IANA Tag Registry
Please see the public online database of [tags](https://www.iana.org/assignments/cbor-tags/cbor-tags.xhtml), where the tags are grouped as follows:

| Tag Range                   | How to Register         |
|-----------------------------|-------------------------|
| 0-23                        | Standards Action        |
| 24-32767                    | Specification Required  |
| 32768-18446744073709551615  | First Come First Served |


- Supports encoding and decoding of various CBOR data types according to [RFC8949](https://datatracker.ietf.org/doc/html/rfc8949) 
- CDDL [RFC8610](https://datatracker.ietf.org/doc/html/rfc8610) support for defining custom data structures

For more examples and detailed documentation, visit our [Wiki](link-to-wiki).

## 🌟 Practical Use Cases

- **IoT Communication**: Efficiently encode sensor data in memory-constrained environments
- **Configuration Serialization**: Save and load application settings with schema validation
- **Cross-Platform Communication**: Exchange data between different systems with a well-defined standard
- **Storage**: Store structured data in a compact binary format

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---