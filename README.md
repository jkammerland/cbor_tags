# C++20 CBOR Library

## Index

1. [Introduction](#intruction)
4. [Features](#features)
5. [Build and Run Tests](#build-and-run-tests)
6. [Requirements](#requirements)
7. [Usage](#usage)
8. [License](#license)

## Introduction

This library provides a C++ implementation for encoding and decoding Concise Binary Object Representation (CBOR) data. CBOR is a data format whose design goals include the possibility of smaller encoded size and extensibility without the need for version negotiation. 

There are many types of cbor objects defined, but the major types are:

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


The name cbor_tags refer to the focus on handling tagged types(6) in a user friendly way. 

"A tagged data item ("tag") whose tag number, an integer in the range 0..264-1 inclusive, is the argument and whose enclosed data item (tag content) is the single encoded data item that follows the head. See [RFC8949#tags](https://www.rfc-editor.org/rfc/rfc8949.html#tags)"

**This means that if you want to make an object for public use, you can define the exact serialization of that object and tag it.**

Please see the public online database of [tags](https://www.iana.org/assignments/cbor-tags/cbor-tags.xhtml), where the tags are grouped as follows:

| Tag Range                   | How to Register         |
|-----------------------------|-------------------------|
| 0-23                        | Standards Action        |
| 24-32767                    | Specification Required  |
| 32768-18446744073709551615  | First Come First Served |

## Features

- Supports encoding and decoding of various CBOR data types according to [RFC8949](https://datatracker.ietf.org/doc/html/rfc8949) (WIP)
- Handles tagged types, lets the user choose how to decode any type without making any copies first
- Supports serialization and deserialization of general CBOR data (WIP)
- Provides partial parsing API, for improved flexibility
- Implements optional full encoding/decoding into standard containers like vector, map, unordered_map and variants (WIP)

## Build and run tests

This will build and run the tests. Currently the tests use doctest, nameof, fmt and zpp_bits. The dependencies are downloaded with cpm cmake and 

```bash
git clone ...
cd cbor_tags
mkdir build && cd build
cmake ..
make -j
ctest
```


## Requirements

- C++20 compatible compiler
- Standard C++ library
- exceptions (for now)

## Usage
basic:


```cpp
using namespace cbor::tags;
auto encoded = encoder::serialize(std::uint64_t(4294967296));
auto decoded = decoder::deserialize(encoded);
REQUIRE(std::holds_alternative<std::uint64_t>(decoded));
CHECK_EQ(std::get<std::uint64_t>(decoded), 4294967296);
```

cbor integers in array type of size 1000:
```cpp
using namespace cbor::tags;
// Make big vector
std::vector<value> big_array;
big_array.reserve(1000);
for (std::uint64_t i = 0; i < 1000; ++i) {
    big_array.emplace_back(i);
}
auto big_array_encoded = encoder::serialize(big_array);

CHECK(big_array_encoded[0] == std::byte{0x99}); // 0x99 is the CBOR header for an array of 1000 elements
CHECK(big_array_encoded[1] == std::byte{0x03}); // First byte of 1000 (0x03E8)
CHECK(big_array_encoded[2] == std::byte{0xE8}); // Second byte of 1000 (0x03E8)

// 1903e7 is 999, the last element
CHECK(big_array_encoded[big_array_encoded.size() - 2] == std::byte{0x03});
CHECK(big_array_encoded.back() == std::byte{0xE7});

fmt::print("Big array: ");
print_bytes(big_array_encoded); // anotated hex/diagnostic notation TODO:

/* anotated hex(2723 bytes):
99 03e8    # array(1000)
   00      #   unsigned(0)
   01      #   unsigned(1)
   02      #   unsigned(2)
   03      #   unsigned(3)
   04      #   unsigned(4)
   05      #   unsigned(5)
   06      #   unsigned(6)
   07      #   unsigned(7)
   08      #   unsigned(8)
   09      #   unsigned(9)
   0a      #   unsigned(10)
   0b      #   unsigned(11)
   0c      #   unsigned(12)
   0d      #   unsigned(13)
   0e      #   unsigned(14)
   0f      #   unsigned(15)
   10      #   unsigned(16)
   11      #   unsigned(17)
   12      #   unsigned(18)
   13      #   unsigned(19)
   14      #   unsigned(20)
   15      #   unsigned(21)
   16      #   unsigned(22)
   17      #   unsigned(23)
   18 18   #   unsigned(24)
   18 19   #   unsigned(25)
   18 1a   #   unsigned(26)
   18 1b   #   unsigned(27)
   18 1c   #   unsigned(28)
   ...
   18 fe   #   unsigned(254)
   18 ff   #   unsigned(255)
   19 0100 #   unsigned(256)
   19 0101 #   unsigned(257)
   19 0102 #   unsigned(258)
   ...
*/

```

json like sorted map with different key types:
```cpp
SUBCASE("Map of float sorted") {
    std::map<cbor::tags::value, cbor::tags::value> float_map;
    float_map.insert({3.0f, 3.14159f});
    float_map.insert({1.0f, 3.14159f});
    float_map.insert({2.0f, 3.14159f});
    float_map.insert({4.0f, 3.14159f});
    float_map.insert({-5.0f, 3.14159f});
    float_map.insert({-1.0f, 3.14159f});
    float_map.insert({10.0f, 3.14159f});
    // Mix in a double
    float_map.insert({3.0, 3.14159f});
    float_map.insert({-3.0, 3.14159f});

    // Mix in a string
    float_map.insert({"hello", 3.14159f});

    // Mix in a bool
    float_map.insert({true, 3.14159f});

    // Mix in a null
    float_map.insert({nullptr, 3.14159f});

    // Mix in some integers
    float_map.insert({1, 3.14159f});
    float_map.insert({-2, 3.14159f});
    float_map.insert({-3, 3.14159f});

    auto encoded = cbor::tags::encoder::serialize(float_map);
    fmt::print("Float map sorted: ");
    print_bytes(encoded);

    /*
    Annotated Hex (140 bytes)
    af                     # map(15)
        22                  #   negative(-3)
        fa 40490fd0         #   float(3.141590118408203)
        21                  #   negative(-2)
        fa 40490fd0         #   float(3.141590118408203)
        01                  #   unsigned(1)
        fa 40490fd0         #   float(3.141590118408203)
        65                  #   text(5)
            68656c6c6f       #     "hello"
        fa 40490fd0         #   float(3.141590118408203)
        fa c0a00000         #   float(-5)
        fa 40490fd0         #   float(3.141590118408203)
        fa bf800000         #   float(-1)
        fa 40490fd0         #   float(3.141590118408203)
        fa 3f800000         #   float(1)
        fa 40490fd0         #   float(3.141590118408203)
        fa 40000000         #   float(2)
        fa 40490fd0         #   float(3.141590118408203)
        fa 40400000         #   float(3)
        fa 40490fd0         #   float(3.141590118408203)
        fa 40800000         #   float(4)
        fa 40490fd0         #   float(3.141590118408203)
        fa 41200000         #   float(10)
        fa 40490fd0         #   float(3.141590118408203)
        fb c008000000000000 #   float(-3)
        fa 40490fd0         #   float(3.141590118408203)
        fb 4008000000000000 #   float(3)
        fa 40490fd0         #   float(3.141590118408203)
        f5                  #   true, simple(21)
        fa 40490fd0         #   float(3.141590118408203)
        f6                  #   null, simple(22)
        fa 40490fd0         #   float(3.141590118408203)

     Diagnostic notation
        {
            -3: 3.14159_2,
            -2: 3.14159_2,
            1: 3.14159_2,
            "hello": 3.14159_2,
            -5.0_2: 3.14159_2,
            -1.0_2: 3.14159_2,
            1.0_2: 3.14159_2,
            2.0_2: 3.14159_2,
            3.0_2: 3.14159_2,
            4.0_2: 3.14159_2,
            10.0_2: 3.14159_2,
            -3.0_3: 3.14159_2,
            3.0_3: 3.14159_2,
            true: 3.14159_2,
            null: 3.14159_2,
        }
    */
    CHECK_EQ(to_hex(encoded),
                "af22fa40490fd021fa40490fd001fa40490fd06568656c6c6ffa40490fd0fac0a00000fa40490fd0fabf800000fa40490fd0fa3f800000fa"
                "40490fd0fa40000000fa40490fd0fa40400000fa40490fd0fa40800000fa40490fd0fa41200000fa40490fd0fbc008000000000000fa4049"
                "0fd0fb4008000000000000fa40490fd0f5fa40490fd0f6fa40490fd0");
}
```

## License
[MIT]