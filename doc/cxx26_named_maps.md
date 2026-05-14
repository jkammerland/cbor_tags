# Named Maps

Named maps encode a struct as a CBOR map using reflected C++ member names as
text-string keys. This requires named reflection, either C++26 static reflection
or the C++20 Boost.PFR field-name backend.

Enable one of the named-reflection backends:

```bash
cmake -B build -DCBOR_TAGS_USE_STD_REFLECTION=ON
cmake -B build -DCBOR_TAGS_USE_BOOST_PFR_NAMES=ON
```

The Boost.PFR backend requires Boost 1.84 or newer, `<boost/pfr/core_name.hpp>`,
and `BOOST_PFR_CORE_NAME_ENABLED`.

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
    .employer = "AcmeCo",
};

std::vector<std::byte> output;
auto enc = make_encoder(output);

// Encodes as:
//
// {
//   "age": 42,
//   "name": "Ada",
//   "employer": "AcmeCo"
// }
enc(as_named_map{person});
```

The encoded CBOR bytes are:

```text
a363616765182a646e616d656341646168656d706c6f7965726641636d65436f
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
Named-map keys are matched as text strings. Encoders produced by this library
emit definite text-string keys for named maps; chunked indefinite text-string
keys are outside the named-map matching contract.

```cpp
Person decoded{};
auto dec = make_decoder(input);

// Decodes from any key order:
//
// {
//   "name": "Ada",
//   "employer": "AcmeCo",
//   "age": 42
// }
dec(as_named_map{decoded});
```

```cpp
// Required fields:
//   Missing required keys are rejected.
//
// Optional fields:
//   Missing optional keys decode as std::nullopt.
//   Explicit CBOR null values are rejected for optional named-map fields.
//
// Duplicate fields:
//   Duplicate known keys are rejected.
//
// Unknown fields:
//   Unknown keys are rejected unless the struct has an extension field.
//
// Extension key storage:
//   Owning key types such as std::string work with contiguous and
//   non-contiguous decoder inputs. Borrowed key types such as
//   std::string_view require contiguous input and must not outlive it.
//
// Extension fields:
//   A flattened named-map shape may contain at most one extension field.
//   Multiple as_named_extension members in the root or nested named groups are
//   unsupported because ownership of an unknown key would be ambiguous.
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

## Larger Example

```cpp
struct AccountOwner {
    // Empty fields are omitted.
    std::optional<std::string> givenName;
    std::optional<std::string> familyName;
};

struct AccountLocation {
    // Required fields must be present when decoding.
    std::string office;
    std::string country;

    // Optional fields are omitted when empty.
    std::optional<std::string> timezone;
};

using OwnerGroup = as_named_group<AccountOwner>;
using LocationGroup = as_named_group<AccountLocation>;
using Metadata = as_named_extension<std::map<std::string, std::string>>;

struct AccountProfile {
    std::string accountId;

    // Both groups are flattened into the same map as accountId.
    OwnerGroup owner;
    LocationGroup location;

    std::vector<std::string> roles;
    std::map<std::string, std::uint32_t> counters;
    std::optional<bool> active;

    // Extra text keys are decoded here as string values.
    Metadata metadata;
};

AccountProfile profile{
    .accountId = "acct-7",
    .owner = OwnerGroup{
        AccountOwner{
            .givenName = std::string{"Ada"},
            .familyName = std::string{"Lovelace"},
        },
    },
    .location = LocationGroup{
        AccountLocation{
            .office = "London",
            .country = "GB",
        },
    },
    .roles = {"admin", "writer"},
    .counters = {{"logins", 42}, {"projects", 3}},
    .active = true,
    .metadata = Metadata{
        {
            {"nickname", "ace"},
            {"team", "compiler"},
        },
    },
};

std::vector<std::byte> output;
auto enc = make_encoder(output);

// Encodes as one flat map:
//
// {
//   "accountId": "acct-7",
//   "givenName": "Ada",
//   "familyName": "Lovelace",
//   "office": "London",
//   "country": "GB",
//   "roles": ["admin", "writer"],
//   "counters": {"logins": 42, "projects": 3},
//   "active": true,
//   "nickname": "ace",
//   "team": "compiler"
// }
enc(as_named_map{profile});
```

Generate CDDL:

```cpp
fmt::memory_buffer schema;

cddl_schema_to<as_named_map<AccountProfile>>(
    schema,
    {
        .row_options = {.format_by_rows = false},
        .root_name = "AccountProfile",
    });

fmt::print("{}\n", fmt::to_string(schema));
```

Output:

```cddl
AccountProfile = {accountId: tstr, owner, location, roles: [* tstr], counters: {* tstr => uint}, ? active: bool, * tstr => tstr}
location = (office: tstr, country: tstr, ? timezone: tstr)
owner = (? givenName: tstr, ? familyName: tstr)
```

```cpp
// Decode checks:
//   "office" and "country" are required because AccountLocation uses
//   plain std::string fields.
//
//   "timezone" may be absent because it is std::optional<std::string>.
//
//   Duplicate known keys are rejected:
//     "givenName" twice
//
//   Duplicate extension keys are rejected:
//     "nickname" twice
//
//   Extension keys must not shadow known fields:
//     "accountId", "givenName", "familyName", "office", "country",
//     "timezone", "roles", "counters", and "active"
//
//   Named-map keys must be text strings.
```
