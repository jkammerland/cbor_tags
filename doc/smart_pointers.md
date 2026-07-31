# Smart Pointer Codecs

Smart-pointer support is opt-in:

```cpp
#include "cbor_tags/extensions/smart_ptr.h"

using namespace cbor::tags;
using namespace cbor::tags::ext::smart_ptr;
```

There are two codecs because the wire formats do different things:

- `unique_ptr_codec` writes an owned value as `T`, or an empty pointer as
  CBOR `null`.
- `shared_ptr_codec` writes CBOR shared-reference tags 28 and 29 so repeated
  pointers decode to the same object.

Both codecs use structural concepts rather than matching only the standard
pointer templates. `IsUniquePointer`, `IsSharedPointer`, and `IsSmartPointer`
therefore accept compatible user-defined pointer types. For a shared pointer,
copying the pointer must retain and share the same object. This semantic rule
cannot be checked by a C++ concept.

## Unique Ownership

```cpp
std::vector<std::byte> bytes;
std::unique_ptr<int> sent = std::make_unique<int>(42);

auto enc = make_encoder<unique_ptr_codec>(bytes);
enc(sent); // encodes the same item as int{42}

std::unique_ptr<int> received;
auto dec = make_decoder<unique_ptr_codec>(bytes);
dec(received);
```

The wire shape is simply:

```text
empty pointer -> null
pointer       -> T
```

Custom deleters are supported. Decoding calls `reset(new T)`, so the
destination pointer keeps its existing deleter.

`T` must be default-initializable for decoding and must not itself accept CBOR
`null`. For example, `std::unique_ptr<std::optional<int>>` is rejected because
the same `null` item could mean either an empty pointer or an empty optional.

## Shared Ownership

No root wrapper is needed:

```cpp
struct message {
    std::shared_ptr<int> first;
    std::shared_ptr<int> second;
};

auto value = std::make_shared<int>(42);
message sent{value, value};

std::vector<std::byte> bytes;
auto enc = make_encoder<shared_ptr_codec>(bytes);
enc(sent);

message received;
auto dec = make_decoder<shared_ptr_codec>(bytes);
dec(received);

assert(received.first == received.second);
```

The first non-null pointer is written with tag 28. Later occurrences are tag
29 plus the earlier table index:

```text
[
  #6.28(42),
  #6.29(0)
]
```

An empty pointer is ordinary CBOR `null`.

The encoder and decoder each keep one reference table. That table belongs to
the codec object and remains across calls:

```cpp
enc(value);  // #6.28(42)
*value = 99;
enc(value);  // #6.29(0), still refers to the first snapshot

enc.reset_shared_ptr_scope();
enc(value);  // #6.28(99), first value in a new table
```

The decoder must receive matching segments in the same order and retain its
table for the same period. Call `reset_shared_ptr_scope()` at the logical
namespace boundary.

The lookup key is the exact pointer type plus `pointer.get()`:

- the same type and address is one object, even if two pointer values use
  different control blocks;
- different addresses are different objects, including aliasing pointers;
- different pointer types are different objects, even at the same address.

The table does not track pointer generations. Destroying an object and creating
another object of the same pointer type at the same address before resetting
the scope can produce a stale tag 29 reference. Reset the scope before pointer
addresses can be reused.

`scoped_shared_ptr<Pointer>` is also a supported pointer value.
`as_scoped_shared_ptr(pointer)` copies an lvalue pointer or moves an rvalue
pointer into that value. It uses the codec's current reference table like the
underlying pointer.

## User-Owned Reference Tables

Applications may supply their own storage and lookup policy:

```cpp
shared_ptr_encode_scope table;
table.reserve(64);

enc.set_shared_ptr_scope(table);
enc(first_segment);
enc(second_segment);

table.reset();
```

Use a matching decode scope on the receiver:

```cpp
shared_ptr_decode_scope table;
dec.set_shared_ptr_scope(table);
dec(first_output);
dec(second_output);
```

`SharedPtrEncodeScope` requires:

```cpp
expected<shared_ptr_observation, status_code>
observe(const shared_ptr_encode_key&);

void mark_complete(std::size_t index);
void reset();
```

`SharedPtrDecodeScope` requires:

```cpp
expected<std::size_t, status_code>
insert(const shared_ptr_decode_entry&);

expected<shared_ptr_decode_entry, status_code>
resolve(std::size_t index);

void mark_complete(std::size_t index);
void reset();
```

Scope indices are host table positions, so their type is `std::size_t`. The
codec converts them to and from CBOR unsigned integers at the tag 29 wire
boundary and rejects a decoded value that the host index type cannot represent.

The supplied object owns capacity limits, allocation strategy, lookup
complexity, and lifetime. It must outlive the codec while installed.
`use_default_shared_ptr_scope()` switches the codec back to its internal table;
that internal table retains any earlier entries until it is reset.

## Deliberate Restrictions

Every non-null pointee must encode as exactly one CBOR item. These are valid:

```cpp
std::shared_ptr<int>                 primitive;
std::shared_ptr<std::vector<int>>    array;
std::shared_ptr<tagged_record>       tagged_item;
std::shared_ptr<one_field_struct>    one_item_struct;
std::shared_ptr<multi_field_struct>  array_wrapped_struct; // default options
```

An empty aggregate encodes no item and is rejected. A multi-field aggregate is
also rejected when group wrapping is disabled, because it would encode several
items under one tag 28.

A shared pointer inside `std::variant` requires an application codec:

```cpp
using choice = std::variant<std::shared_ptr<int>, std::string>;

enc(choice{}); // compile-time error
```

The application must choose and encode an unambiguous discriminator, normally
an application tag. The library does not guess how tag 28/29 should compete
with the other alternatives. Unique-pointer variants remain supported when
their wire shapes do not overlap.

An ordinary `std::optional` cannot directly contain a smart pointer. Both the
empty optional and the empty pointer would encode as CBOR `null`. A named-map
field can still distinguish an omitted optional field from a present field
whose pointer value is `null`.

Cycles are rejected. A tag 29 reference may only target a completed tag 28
entry. The codec does not prewalk the graph and does not roll back after a
failure.

One-shot decode is terminal as elsewhere in the library. If a pointee decode
returns `incomplete`, the allocated pointer and decoded prefix remain in the
destination, and the scope remains in its terminal failed state. Reset or
discard both before reuse.

Input ownership, framing, admission limits, and truthful C++ range semantics
remain the caller's responsibility. The decoder parses the requested CBOR
segment once and returns `incomplete` if that segment ends early.

## CDDL

`std::unique_ptr<T>` renders as:

```cddl
T / null
```

A shared pointer renders directly as:

```cddl
null / #6.28(T) / #6.29(uint)
```

```cpp
cddl_schema_to<std::shared_ptr<int>>(schema);
```

CDDL describes item shapes. It cannot express whether a tag 29 index exists,
has the requested pointer type, or refers to a completed entry. Those checks
remain runtime scope checks.
