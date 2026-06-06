# A C++20 CBOR Library with Automatic Reflection

[![macOS CI](https://github.com/jkammerland/cbor_tags/actions/workflows/macos.yml/badge.svg)](https://github.com/jkammerland/cbor_tags/actions/workflows/macos.yml)
[![Ubuntu CI](https://github.com/jkammerland/cbor_tags/actions/workflows/ubuntu.yml/badge.svg)](https://github.com/jkammerland/cbor_tags/actions/workflows/ubuntu.yml)
[![Windows CI](https://github.com/jkammerland/cbor_tags/actions/workflows/windows.yml/badge.svg)](https://github.com/jkammerland/cbor_tags/actions/workflows/windows.yml)
[![Quality CI](https://github.com/jkammerland/cbor_tags/actions/workflows/quality.yml/badge.svg)](https://github.com/jkammerland/cbor_tags/actions/workflows/quality.yml)

This is a library for encoding and decoding Concise Binary Object Representation (CBOR) data. CBOR is a data format designed for small encoded sizes and extensibility without version negotiation. As an information model, CBOR is a superset of JSON, supporting additional data types and custom type definitions via tags 🏷️. Some good examples of different binary formats can be found here [rfc8949-name-conciseness-on-the-wire](https://www.rfc-editor.org/rfc/rfc8949.html#name-conciseness-on-the-wire). Also obligatory [xkcd/927](https://xkcd.com/927/). 

The primary advantage of using this library is the ability to define your own data structures and encode/decode them in a way that is both efficient and easy to distribute. All another party needs is to know the tag number and the Concise Data Definition of the object. If using this library on both ends, just the struct definition is enough to encode/decode the data.

See standard specifications for more information:
- CDDL [RFC8610](https://datatracker.ietf.org/doc/html/rfc8610) (Concise Data Definition Language)
- CBOR [RFC8949](https://datatracker.ietf.org/doc/html/rfc8949) (Concise Binary Object Representation)
- CBOR Typed Arrays [RFC8746](https://datatracker.ietf.org/doc/html/rfc8746)

The library design is inspired by [zpp_bits](https://github.com/eyalz800/zpp_bits) and [bitsery](https://github.com/fraillt/bitsery), but uses the CBOR standard as binary format instead of a custom one. It also provides intuitive error handling and gives full control over memory layouts and buffer management. By supporting all standard containers and abstractions, the API for handling the data before/after encoding/decoding should be familiar without requiring detailed knowledge of the CBOR format.


# Index

- [🎯 Key Features](#-key-features)
- [🔧 Quick Start](#-quick-start)
  - [Basic Encoding/Decoding Example](#basic-encodingdecoding-example)
  - [Tagged Struct Example](#tagged-struct-example)
  - [Advanced Type Support](#advanced-type-support)
  - [Version Handling with Variants](#version-handling-with-variants)
  - [Manual Tag Parsing](#manual-tag-parsing)
  - [Private class members or explicit overloading](#private-class-members-or-explicit-overloading)
- [✨ Advanced Features](#-advanced-features)
- [🎨 Custom Tag Handling](#-custom-tag-handling)
- [🔄 Automatic Reflection](#-automatic-reflection)
- [🏷️ Annotating buffers and Diagnostic notation](#️-annotating-cbor-buffers)
- [🤝 CDDL Schema Generation](#-cddl-schema-generation)
- [Smart Pointer Codecs](#smart-pointer-codecs)
- [✅ Requirements](#-requirements)
- [📦 Installation](#-installation)
- [💡 CMake Integration](#-cmake-integration)
- [Limiting Decode Allocation With `std::pmr`](#limiting-decode-allocation-with-stdpmr)
- [✨ WIP Features](#-wip-features)
- [📚 Documentation](#-documentation)
  - [IANA Tag Registry](#iana-tag-registry)
- [🌟 Practical Use Cases](#-practical-use-cases)
- [ Testing Performance and Benchmarks](#testing-performance-and-benchmarks)
- [📄 License](#-license)


---

## 🎯 Key Features

- Support for both contiguous and non-contiguous buffers.
- Buffer-backed decode views for contiguous and non-contiguous inputs.
- Flexible tag handling for structs and tuples, can be completely non-invasive on your code.
- Support for many (almost arbitrary) containers and nesting.
- noexcept API (encode/decode), with status return values using `tl::expected<void, status_code>` by default or `std::expected<void, status_code>` with C++23 opt-in.
- Opt-in [RFC 8746 typed-array](doc/rfc8746_typed_arrays.md) codec for homogeneous numeric payloads.
- CDDL support for schema and custom data definitions.
- When using C++26, all features are available with no extra dependencies.
- Upcoming: resumable encoding and decoding (useful for streaming usecases).

## 🔧 Quick Start
### Basic Encoding/Decoding Example
Basic example of encoding and decoding a single cbor items:
```cpp
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cassert>
#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>
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
    auto transmitted_data = data;

    // Declare variables to hold the decoded data (receiver side)
    int  a;
    double b;
    std::string c;

    // Decoding
    auto dec = make_decoder(transmitted_data);
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

Tagged tagged{.a = 2, .b = 3.14, .c = "Hello, World!"};
enc(tagged);
```

> [!NOTE]
> The definition of a tag is a CBOR major type 6 encoded uint, with a concise data definition format of #6.321(any). This allows generic parsers to decode tags without knowing their semantic meaning or the exact order of internal items. It also means the struct implicitly define a cbor array of exact types following the tag, i.e #6.321([int, float64, tstr]). See tuning options for more details.

Equivalent to manually encoding the struct in the following example:
```cpp
Tagged tagged{.a = 2, .b = 3.14, .c = "Hello, World!"};
enc(static_tag<Tagged::cbor_tag>{}, as_array{3}, tagged.a, tagged.b, tagged.c); // same as enc(tagged);
// Also equivalent to:
// enc(static_tag<Tagged::cbor_tag>{}, wrap_as_array{tagged.a, tagged.b, tagged.c});
// Now the buffer contains the tag(321) followed by a single array with 3 elements

```

### Advanced Type Support
This can be taken further to any number of members or nesting, e.g a struct with all CBOR major types (and more):
```cpp
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
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
    std::string                b;  // Major type 3 (text string)
    std::vector<std::byte>     c;  // Major type 2 (byte string)
    std::vector<int>           d;  // Major type 4 (array)
    std::map<int, std::string> e;  // Major type 5 (map)
    struct B {
        static_tag<123123> cbor_tag; // Major type 6 (tag)
        bool             a;        // Major type 7 (simple value)
    } f;
    double g;                      // Major type 7 (float)

    // More advanced types
    std::variant<int, std::string, std::vector<int>> h; // Major type 0, 1, 3 or 4
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
> The encoding is basically just a "tuple cast", that a fold expression apply encode(...) to, for each member. The definition of the struct is what sets the expectation when decoding the data. Any mismatch when decoding will result in a status_code, i.e result.error(). An incomplete decode will result in status_code "incomplete". The primary decoder is still a one-shot API: retry/resume after incomplete input is reserved for a future explicit resumable decoder entry point.

### Version Handling with Variants
The example below show how cbor tags can be utilized for version handling. There is no explicit version handling in the protocol, instead a tag can represent a new object, which *you* the application developer can, by your definition, decide to be a new version of an object.
```cpp
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <variant>
#include <string>
#include <type_traits>
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
    enc(make_tag_pair(static_tag<0x10>{}, Api1{.a = 42, .b = 43}));

    // Encode a 0 length binary string in the middle of the buffer
    enc(std::vector<std::byte>{});

    // Encode Api2 with a tag of 0x20
    enc(make_tag_pair(static_tag<0x20>{}, Api2{"hello", "world"}));

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

### Private Class Members or explicit overloading
Should the need arise for overloading, or encoding private members, you have two options. The first is to use the `Access` friend class as shown in the example above. This will allow you to access private members of your class for encoding/decoding purposes.

```cpp
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include <vector>
#include <string>

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

    /* 
       IMPORTANT: The tag below is always used when encoding/decoding automatically.
       It is optional to define unless the class is used with std::variant.
    */
    static_tag<12345> cbor_tag; 
    
    // Method 1: Use member functions for encoding/decoding
    friend cbor::tags::Access;  // Grant access to the library
    
    // You need one or both of these functions, depending on your needs
    // Remember that if this is an object, wrap it in a array or map, as 
    // single items is most likely not what you want. 
    // But if there is only 1 item you don't need any wrapping of course.

    template <typename Encoder>
    constexpr auto encode(Encoder& enc) /* Must be const -> */ const /* <- Must be const */ {
        // Do not encode the tag manually, it is handled by the library
        return enc(wrap_as_array{id_, name_});  // Encode as array with 2 elements
    }
    
    template <typename Decoder>
    constexpr auto decode(Decoder& dec) {
        // Do not decode the tag manually, it is handled by the library
        return dec(wrap_as_array{id_, name_});  // Decode as array with 2 elements
    }
};

int main() {
    // Create an object with private data
    PrivateDataClass obj{123, "Private Data"};
    
    // Encode the object
    std::vector<uint8_t> buffer;
    auto                 enc = make_encoder(buffer);
    { [[maybe_unused]] auto _ = enc(obj); }

    // Decode into a new object
    auto             dec = make_decoder(buffer);
    PrivateDataClass decoded_obj;
    { [[maybe_unused]] auto _ = dec(decoded_obj); }
    
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
// Without the cbor_tag(), it will be encoded as an array item only
constexpr auto cbor_tag(const ExternalClass&) { return static_tag<54321>{}; }

// Encode function - converts the object to CBOR
template <typename Encoder>
constexpr auto encode(Encoder& enc, const ExternalClass& obj) {
    // Access the private data through getters
    return enc(wrap_as_array{obj.getId(), obj.getName()});
}

// Decode function - reconstructs the object from CBOR
// IMPORTANT: Must use "&&" in the overloaded type to match free function signature, and to allow temporary objects */ 
template <typename Decoder>
constexpr auto decode(Decoder& dec, ExternalClass /* --> */ && /* <--*/ obj) {
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

> [!NOTE]
> You can mix and match these methods above. For example, you can can use a free form cbor_tag with member functions for encoding/decoding. Or you could use a static tag with free form encode/decode methods. By not defining a tag or a cbor_tag() function you can also manually encode/decode the tag inside of your encode/decode functions.

More examples on alternative methods can be found in wiki, such as how to handle non-default constructible types.

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

Reflection is fully automatic, and pre-C++26 a codegen tool (see below) can be used to extend the max number of members (default is 24), with no upper limit.
Any level of nesting will work, it's only the individual struct sizes that are limited pre-C++26.

The API is the same in both modes:

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
> [!IMPORTANT]
> This manual `std::apply(...)` step is only illustrative; the encoder and decoder call operators do it for you and stop at the first error. For generated C++20 reflection, `CBOR_TAGS_REFLECTION_RANGES` controls the generated aggregate sizes and defaults to `"1:24"`. Use `-DCBOR_TAGS_BUILD_TOOLS=ON -DCBOR_TAGS_REFLECTION_RANGES="..."` if you need larger or custom ranges.

Native C++26 reflection is explicit and opt-in for now. When consuming code is compiled with `__cpp_impl_reflection >= 202506L`, `to_tuple(...)` uses `std::meta` to enumerate aggregate members directly. GCC currently requires `-std=gnu++26 -freflection`. Configure this project with `-DCBOR_TAGS_USE_STD_REFLECTION=ON` to build and run the tests with native reflection enabled.

Named-map reflection can also be enabled in C++20 with Boost.PFR field names. Configure with `-DCBOR_TAGS_USE_BOOST_PFR_NAMES=ON`, or define `CBOR_TAGS_USE_BOOST_PFR_NAMES=1` before including cbor_tags headers. This requires Boost.PFR with `<boost/pfr/core_name.hpp>` and `BOOST_PFR_CORE_NAME_ENABLED` (Boost 1.84 or newer). CMake builds with this option require a Boost package config that exports `Boost::headers`; installed packages export the compile definition and Boost dependency only when they were built with the Boost.PFR names option enabled.

CDDL enum value names use native C++26 reflection when `CBOR_TAGS_USE_STD_REFLECTION=ON`, or magic_enum in C++20 builds. For C++20, configure with `-DCBOR_TAGS_USE_MAGIC_ENUM_NAMES=ON`, or define `CBOR_TAGS_USE_MAGIC_ENUM_NAMES=1` before including `cbor_tags/extensions/cbor_visualization.h`. This requires a `magic_enum` package config that exports `magic_enum::magic_enum`; installed packages export the compile definition and dependency only when they were built with the magic_enum names option enabled. Existing schemas keep rendering enums as `uint` or `int` unless `CDDLOptions::enum_mode` is set to `CDDLEnumMode::named_values`.

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
buffer_annotate(data, buffer, {.mode = AnnotationMode::no_annotation}); // Request only hex view
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

Smart annotation is the default and adds a semantic right-hand column:
```cpp
std::vector<std::byte> data = to_bytes("bf6346756ef563416d7421ff");

fmt::memory_buffer annotation;
buffer_annotate(data, annotation, {
    .annotation_column = 13
});
```
Output:
```
bf           # map(*)
   63        #   text(3)
      46756e #     "Fun"
   f5        #   true, simple(21)
   63        #   text(3)
      416d74 #     "Amt"
   21        #   negative(-2)
   ff        #   break
```
In smart mode, headers are padded to `annotation_column`. Text and byte string
payload bytes wrap before that column so the annotation stays aligned. Malformed
CBOR, excessive nesting, configured input/output size limits, and layouts too
narrow to show data without truncation throw `std::runtime_error`. Set
`.mode = AnnotationMode::no_annotation` to request the plain hex view.
Invalid UTF-8 text payloads render as `non-utf8(N)`, where `N` is byte length.

The same visualization helpers are available from the optional CLI tool. Build
it with `-DCBOR_TAGS_BUILD_TOOLS=ON`:

```bash
cmake -B build -G Ninja -DCBOR_TAGS_BUILD_TOOLS=ON
cmake --build build --target cbor_tags_cli
```

The CLI requires explicit input encoding, accepts data as an argument or on
stdin, and supports both standard and URL-safe base64:

```bash
# Smart annotation from hex
cbor_tags_cli annotate --input hex --annotation-column 13 bf6346756ef563416d7421ff

# Diagnostic notation from base64 on stdin
printf 'Ymhp\n' | cbor_tags_cli diagnostic --input base64

# URL-safe base64 that starts with '-' needs -- to end option parsing
cbor_tags_cli annotate --input base64 -- -n-AAAA

# Hex input may contain whitespace and # comments
cbor_tags_cli annotate --input hex 'bf 63 46756e # "Fun"
f5 63 416d74 21 ff'

# Validate text-string UTF-8 in diagnostic output
cbor_tags_cli diagnostic --input hex --no-format-by-rows --check-tstr-utf8 62c328
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
A = [uint, nint, int, float64, float32, bool, tstr, bstr, {* int => tstr}, int / tstr, int / null, B, C]
C = #6.141([int, tstr, B / null])
B = #6.140([bstr, {* int => tstr}])
```
Structs can also be encoded as maps where each key is the C++ field name.
For example, a `Person` struct can be encoded as:

```text
{"age": 42, "name": "Ada", "employer": "Coretura"}
```

See [named maps](doc/cxx26_named_maps.md) for serialization,
deserialization, CDDL, and exact output examples.

Standards coverage is tracked in [`doc/cddl_standard_coverage.md`](doc/cddl_standard_coverage.md).

### Smart Pointer Codecs
Smart pointer support is opt-in through `cbor_tags/extensions/smart_ptr.h`.
Use `nullable_ptr_codec` for nullable ownership values, or `shared_graph_codec`
when repeated `std::shared_ptr<T>` identity must be preserved across one logical
graph session:

```cpp
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <memory>

using namespace cbor::tags;
using namespace cbor::tags::ext::smart_ptr;

std::vector<std::byte> buffer;

auto shared = std::make_shared<int>(42);
shared_graph_encode_session encode_graph;
auto enc = make_encoder<shared_graph_codec>(buffer);

enc(as_shared_graph(encode_graph, shared));
enc(as_shared_graph(encode_graph, shared));

std::shared_ptr<int> first;
std::shared_ptr<int> second;

auto dec = make_decoder<shared_graph_codec>(buffer);
shared_graph_decode_session decode_graph;

dec(as_shared_graph(decode_graph, first));
dec(as_shared_graph(decode_graph, second));
```

`nullable_ptr_codec` encodes null pointers as `[0]` and values as `[1, value]`.
`shared_graph_codec` uses CBOR value-sharing tags 28/29 inside
`as_shared_graph(...)` roots. Use `shared_graph_cddl<T>` when generated CDDL
should describe that graph shape, rendering `std::shared_ptr<T>` as
`[0] / #6.28(T) / #6.29(uint)`. See
[Smart Pointer Codecs](doc/smart_pointers.md) for wire shapes, limitations,
value-sharing spec links, CDDL examples, and variant behavior.
See [Codec Extensions](doc/codec_extensions.md) for the general opt-in extension
pattern.


## ✅ Requirements

- tl::expected by default, or C++23 `<expected>` when `CBOR_TAGS_USE_STD_EXPECTED=ON`.
- fmt 11.0.2 or newer for the default C++20/C++23 formatting and type-name path.
- nameof 0.10.4 or newer for the default C++20/C++23 type-name path.
- C++20 compatible compiler, tested with GCC 12-16, LLVM/Clang 17-22, Visual Studio Clang-CL, MSVC-latest, and AppleClang 16/26.
- Optional C++26 static reflection support, currently tested with GCC 16 using `-std=gnu++26 -freflection`.
- Optional C++26 STL-only mode with `CBOR_TAGS_STL_ONLY=ON`; this uses `std::expected`, `std::format`, and `std::meta` and exports no fmt, nameof, or tl::expected dependency.
- Optional C++20 named-map support through Boost.PFR field names, requiring Boost 1.84 or newer, `BOOST_PFR_CORE_NAME_ENABLED`, and a Boost CMake package config when enabled through CMake.
- Optional CDDL enum-name support through C++26 static reflection or magic_enum 0.9.7 or newer.
- CMake 3.20+ for raw `cmake -S/-B` builds, 3.25+ when building an installed CMake package with `CBOR_TAGS_INSTALL=ON`, and 3.31+ for the checked-in preset workflows.

## 📦 Installation

Standard CMake:

```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
  cbor_tags
  GIT_REPOSITORY https://github.com/jkammerland/cbor_tags.git
  GIT_TAG v0.20.0 # or a newer release/commit for newer extension features
)

FetchContent_MakeAvailable(cbor_tags)

add_executable(my_target main.cpp)
target_link_libraries(my_target PRIVATE cbor::tags)
```

System-wide install:

```bash
git clone https://github.com/jkammerland/cbor_tags
cd cbor_tags
cmake -B build -DCBOR_TAGS_INSTALL=ON -DCBOR_TAGS_USE_SYSTEM_EXPECTED=ON
cmake --build build
cmake --install build
```

`CBOR_TAGS_INSTALL=ON` requires CMake 3.25 or newer because the install package
helper is target_install_package.cmake v7. Installed CMake packages also need a
non-FetchContent expected backend: use `CBOR_TAGS_USE_SYSTEM_EXPECTED=ON` for
`tl::expected`, or C++23 with `CBOR_TAGS_USE_STD_EXPECTED=ON` for
`std::expected` return values.

C++23 `std::expected` return backend:

```bash
cmake -B build \
  -DCMAKE_CXX_STANDARD=23 \
  -DCMAKE_CXX_STANDARD_REQUIRED=ON \
  -DCBOR_TAGS_INSTALL=ON \
  -DCBOR_TAGS_USE_STD_EXPECTED=ON
```

C++26 STL-only installed package:

```bash
cmake --preset=release-cxx26-stl-only
cmake --build --preset=release-cxx26-stl-only
```

This mode requires a compiler with C++26 static reflection support. The current
CI path uses GCC on Fedora with `-std=gnu++26 -freflection`. Installed consumers
link only `cbor::tags`; no fmt, nameof, or tl::expected package is exported.

Package-manager opt-in examples:

```bash
conan install . -o cbor-tags/*:std_expected=True -s compiler.cppstd=23
conan install . -o cbor-tags/*:stl_only=True -s compiler.cppstd=26
conan install . -o cbor-tags/*:boost_pfr_names=True
conan install . -o cbor-tags/*:magic_enum_names=True
vcpkg install --x-no-default-features --x-feature=stl-only
vcpkg install --x-feature=boost-pfr-names
vcpkg install --x-feature=magic-enum-names
```

The package features install the optional dependencies; still configure
`cbor_tags` with the matching CMake option when building an enabled install.

## 💡 CMake Integration

```cmake
find_package(cbor_tags REQUIRED)
target_link_libraries(your_target PRIVATE cbor::tags)
```

## ⚙️ C++20 Requirements Note

> [!NOTE]
> This library requires C++20 features. However, you can isolate that requirement by wrapping this library and exposing an API compatible with your target C++ version.

## Limiting Decode Allocation With `std::pmr`

When decoding untrusted CBOR into owning containers, an input can declare very
large array, map, text-string, or byte-string sizes. The decoder does not impose
schema or application size limits for you. If you need allocation containment,
decode into allocator-aware types backed by a bounded `std::pmr::memory_resource`.

This assumes the input byte buffer itself is already bounded by your transport,
framing layer, file-size limit, request-body cap, or another application-level
guard. A PMR arena limits allocations made while materializing decoded C++
values; it does not limit how much CBOR input you accept.
Default decoders also reject raw and materialized CBOR values nested deeper
than 256 structural containers or tags; use `max_decode_depth<N>` in a custom
decoder option set to choose a different nesting limit.

Custom decoders that manually read a container or tag header and then decode
its payload should use the depth-managed scoped helpers. The returned object
must stay alive until the payload has been decoded.

```cpp
namespace ct = cbor::tags;

struct Claims {
    std::int64_t issuer{};

    template <typename Dec>
    ct::expected<void, ct::status_code> decode(Dec& dec) {
        auto map = dec.enter_map(1);
        if (!map) {
            return ct::unexpected<ct::status_code>{map.error()};
        }

        std::int64_t key{};
        auto key_result = dec(key);
        if (!key_result) {
            return key_result;
        }

        auto value_result = dec(issuer);
        if (!value_result) {
            return value_result;
        }

        return {};
    }
};
```

The split header adapters (`as_array`, `as_map`, `as_array_any`, `as_map_any`,
`static_tag`, and `dynamic_tag`) intentionally read only the current header.
They do not keep a depth scope alive for later `dec(...)` calls. Use `enter_*`
when the custom decoder owns the payload decode and needs depth enforcement for
that payload.

```cpp
#include <array>
#include <cstddef>
#include <memory_resource>
#include <string>
#include <vector>

#include <cbor_tags/cbor.h>

// Example input: ["a", "b"].
// In production, populate this from a size-capped input path.
std::vector<std::byte> input{
    std::byte{0x82}, std::byte{0x61}, std::byte{'a'}, std::byte{0x61}, std::byte{'b'},
};

std::array<std::byte, 4096> arena_storage{};
std::pmr::monotonic_buffer_resource arena(
    arena_storage.data(),
    arena_storage.size(),
    std::pmr::null_memory_resource());

std::pmr::vector<std::pmr::string> values{&arena};

auto dec = cbor::tags::make_decoder(input);
auto result = dec(values);

if (!result && result.error() == cbor::tags::status_code::out_of_memory) {
    // The bounded arena was exhausted.
}
```

This can limit allocation injection for PMR-aware layouts such as:

```cpp
std::pmr::vector<std::pmr::string>
std::pmr::vector<std::pmr::vector<int>>
std::pmr::map<std::pmr::string, std::pmr::string>
std::pmr::vector<std::optional<std::pmr::string>>
```

Important limitations(TODO):

- This is allocation containment, not schema validation.
- Use an external scan or application policy when you need max array, map, or string sizes.
- `std::variant` alternatives do not currently receive parent PMR allocator context.
- A bounded arena must use a bounded upstream resource, e.g `std::pmr::null_memory_resource()`.

A future scanning pass is planned for policy checks before materializing values.
That pass should be the right place to reject messages by declared array, map,
or string sizes or other schema/application limits without allocating the target
object graph first.

## ✨ WIP Features

- Done: `std::variant` support, allowing multiple types to be accepted when seen on the buffer (e.g., tagged types representing a versioned object).
- WIP / experimental: range wrappers, raw encoded views, lazy tag scanning, and segmented output for zero-copy-oriented encoding. See [Experimental Range And Segment APIs](doc/experimental_ranges.md).
- TODO: Coroutine support for decoding and encoding, more convenient api wrapper when streaming.
- TODO: Options for encoder/decoder, such as (un)expected type tuning.
- TODO: Performance tuning options, such as disabling some checks/safety and non-standard encodings.
- Done: opt-in nullable `unique_ptr`/`shared_ptr` codec and explicit `shared_ptr` graph codec extensions.

## 📚 Documentation

Additional docs:

- [Custom Codec 1](doc/custom_codec_1.md)
- [Encoder And Decoder Options](doc/options.md)
- [Codec Extensions](doc/codec_extensions.md)
- [RFC 8746 Typed Arrays](doc/rfc8746_typed_arrays.md)
- [Smart Pointer Codecs](doc/smart_pointers.md)
- [Experimental Range And Segment APIs](doc/experimental_ranges.md)

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

The core decoder preserves text-string bytes and does not validate UTF-8 unless
you explicitly request diagnostic UTF-8 checking in tooling.

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

Additional focused examples and design notes live under [`doc/`](doc/), including codec extensions, typed arrays, smart pointers, CDDL handling, and named maps.

## Testing Performance and Benchmarks

Tests can be built after configuring with `-DCBOR_TAGS_BUILD_TESTS=ON`, which will create a target called `tests`. This is what the CI currently runs for all supported compilers/platforms.

Opt-in interoperability tests are available for CBOR behavior across other
stacks. The `nlohmann/json` and Glaze tests cover cross-stack CBOR reads and
known mismatch cases such as CBOR tags, duplicate keys, and typed arrays. The
Rust `cbor-diag` test validates CBOR bytes generated by `cbor_tags` through
`parse_hex`, `parse_bytes`, diagnostic notation round-tripping, and malformed
edge cases.

```bash
cmake --workflow --preset=debug-interop
```

Or configure the options directly:

```bash
cmake -S . -B build/interop-tests -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCBOR_TAGS_BUILD_TESTS=ON \
  -DCBOR_TAGS_BUILD_INTEROP_TESTS=ON \
  -DCBOR_TAGS_INTEROP_NLOHMANN=ON \
  -DCBOR_TAGS_INTEROP_GLAZE=ON \
  -DCBOR_TAGS_INTEROP_CBOR_DIAG=ON
cmake --build build/interop-tests --parallel
ctest --test-dir build/interop-tests --output-on-failure
```

The benchmarks can be run by configuring with `-DCBOR_TAGS_BUILD_BENCHMARKS=ON`.
This creates targets for the encoder, decoder, ranges, and the
`custom_codec_1` comparison suite.

```bash
cmake -S . -B build-bench -G Ninja -DCMAKE_BUILD_TYPE=Release -DCBOR_TAGS_BUILD_BENCHMARKS=ON
cmake --build build-bench --target bench_encoder
cmake --build build-bench --target bench_decoder
cmake --build build-bench --target bench_ranges
cmake --build build-bench --target bench_custom_codec_1
./build-bench/benchmarks/custom_codec_1/bench_custom_codec_1
```

`bench_custom_codec_1` compares the extension against the default CBOR codec for
fixed tagged aggregate and numeric-vector payloads. It also includes RFC 8746
typed-array rows for the same homogeneous numeric vectors, including borrowed
segment encode and borrowed-view decode paths.

Use release builds for timing numbers and run benchmark executables directly
when collecting results. The regular CI test path still builds and runs the
unit test target.

### Test Logging

Unit tests rely on doctest's `INFO` context for diagnostics, so log lines now surface only when an assertion fails. Set the environment variable `CBOR_TAGS_TEST_LOGS=1` to force the helper logs to emit immediately via `MESSAGE`, for example:

```bash
CBOR_TAGS_TEST_LOGS=1 ./build/test/tests --reporter=console
```

Leave the variable unset (default) to keep passing runs quiet while still capturing context on failures.

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---
