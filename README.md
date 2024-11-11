# C++20 CBOR Library (WIP)

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
// TODO
```

cbor integers in array type of size 1000:
```cpp
// TODO
```

## Important notes about interpretation of RFC8949

This is a low level parser, that only verify that the data is well-formed, not the data's validity. The validity of well-formed cbor can/should be handled by the application layer, i.e the consumer of this library. This also makes it more flexible. See [RFC8949#validity-of-items](https://datatracker.ietf.org/doc/html/rfc8949#name-validity-of-items)

 5.3. Validity of Items

```
A well-formed but invalid CBOR data item (Section 1.2) presents a problem with interpreting the data encoded in it in the CBOR data model. A CBOR-based protocol could be specified in several layers, in which the lower layers don't process the semantics of some of the CBOR data they forward. These layers can't notice any validity errors in data they don't process and MUST forward that data as-is. The first layer that does process the semantics of an invalid CBOR item MUST pick one of two choices:
Replace the problematic item with an error marker and continue with the next item, or
Issue an error and stop processing altogether. A CBOR-based protocol MUST specify which of these options its decoders take for each kind of invalid item they might encounter.Such problems might occur at the basic validity level of CBOR or in the context of tags (tag validity).
```

## License
[MIT]
