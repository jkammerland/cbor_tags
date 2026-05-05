# C++26 Named Maps

Named maps encode a struct as a CBOR map using reflected C++ member names as
text-string keys. This requires C++26 static reflection.

All examples below assume:

```cpp
using namespace cbor::tags;
```

## Basic Example

```cpp
struct Person {
    int age;
    std::string name;
    std::string employer;
};

Person person{
    .age = 42,
    .name = "Ada",
    .employer = "OpenAI",
};

std::vector<std::byte> output;
auto enc = make_encoder(output);

// Encodes as:
//
// {
//   "age": 42,
//   "name": "Ada",
//   "employer": "OpenAI"
// }
enc(as_named_map{person});
```

The encoded CBOR bytes are:

```text
a363616765182a646e616d656341646168656d706c6f796572664f70656e4149
```

Generate CDDL for the same shape:

```cpp
fmt::memory_buffer schema;

cddl_schema_to<as_named_map<Person>>(
    schema,
    {
        .row_options = {.format_by_rows = false},
        .root_name = "person",
    });

fmt::print("{}\n", fmt::to_string(schema));
```

Output:

```cddl
person = {age: int, name: tstr, employer: tstr}
```

## Decode Behavior

Named-map decoding is key based, so map entries may arrive in any order.

```cpp
Person decoded{};
auto dec = make_decoder(input);

// Decodes from any key order:
//
// {
//   "name": "Ada",
//   "employer": "OpenAI",
//   "age": 42
// }
dec(as_named_map{decoded});
```

```cpp
// Required fields:
//   Missing required keys are rejected.
//
// Duplicate fields:
//   Duplicate known keys are rejected.
//
// Unknown fields:
//   Unknown keys are rejected unless the struct has an extension field.
```

## Optional Fields, Groups, And Extension Keys

```cpp
struct NameComponents {
    // Empty optionals are omitted from the encoded map.
    std::optional<std::string> firstName;
    std::optional<std::string> familyName;
};

using NameComponentsGroup = as_named_group<NameComponents>;
using StringExtensions = as_named_extension<std::map<std::string, std::string>>;

struct PersonalData {
    // Optional field:
    // Encoded only when it has a value.
    std::optional<std::string> displayName;

    // Named group:
    // NameComponents is flattened into the surrounding map.
    NameComponentsGroup NameComponents;

    // Optional field:
    // Encoded only when it has a value.
    std::optional<std::uint32_t> age;

    // Typed extension map:
    // Unknown text keys are captured here as string values.
    StringExtensions extensions;
};

PersonalData data{
    .NameComponents = NameComponentsGroup{
        NameComponents{
            .firstName = std::string{"Ada"},
        },
    },
    .age = 42,
    .extensions = StringExtensions{
        {
            {"nickname", "ace"},
        },
    },
};

std::vector<std::byte> output;
auto enc = make_encoder(output);

// Encodes as one flat map:
//
// {
//   "firstName": "Ada",
//   "age": 42,
//   "nickname": "ace"
// }
enc(as_named_map{data});
```

The encoded CBOR bytes are:

```text
a36966697273744e616d656341646163616765182a686e69636b6e616d6563616365
```

Generate CDDL:

```cpp
fmt::memory_buffer schema;

cddl_schema_to<as_named_map<PersonalData>>(
    schema,
    {
        .row_options = {.format_by_rows = false},
        .root_name = "PersonalData",
    });

fmt::print("{}\n", fmt::to_string(schema));
```

Output:

```cddl
PersonalData = {? displayName: tstr, NameComponents, ? age: uint, * tstr => tstr}
NameComponents = (? firstName: tstr, ? familyName: tstr)
```

The group member is not encoded as a nested map.

```cpp
// Encoded:
//
// {
//   "firstName": "Ada",
//   "age": 42,
//   "nickname": "ace"
// }
//
// Not encoded:
//
// {
//   "NameComponents": {
//     "firstName": "Ada"
//   },
//   "age": 42,
//   "nickname": "ace"
// }
```

Extension fields only receive keys that do not match a known field or group
field.

```cpp
// Known keys:
//   "displayName", "firstName", "familyName", and "age"
//
// Extension keys:
//   "nickname" is decoded into:
//
//   data.extensions.value_["nickname"]
//
// Without the extension field:
//   "nickname" is rejected as an unknown key.
```
