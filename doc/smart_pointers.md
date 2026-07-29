# Smart Pointer Codecs

Smart pointers are opt-in:

```cpp
#include "cbor_tags/extensions/smart_ptr.h"

#include <cassert>
#include <cstddef>
#include <memory>
#include <vector>

using namespace cbor::tags;
using namespace cbor::tags::ext::smart_ptr;
```

The two codecs have separate jobs:

- `unique_ptr_codec` encodes `std::unique_ptr<T>` as either CBOR `null` or
  `T`. It does not add a wrapper.
- `shared_ptr_codec` preserves `std::shared_ptr` identity. Repeated pointers
  decode to the same object.

`shared_ptr_codec` does not encode a `std::shared_ptr` unless it is inside an
explicit shared-pointer root. This prevents a shared pointer from silently
falling back to ordinary value-copy behavior.

## `std::unique_ptr`

```cpp
std::vector<std::byte> bytes;
std::unique_ptr<int> sent = std::make_unique<int>(42);

auto enc = make_encoder<unique_ptr_codec>(bytes);
enc(sent);                         // same CBOR item as int{42}

std::unique_ptr<int> received;
auto dec = make_decoder<unique_ptr_codec>(bytes);
dec(received);
```

The wire shape is:

```text
nullptr  -> null
pointer  -> T
```

`T` must be default-initializable for decoding. It must not itself accept CBOR
`null`, because `null` would then have two meanings. For example,
`std::unique_ptr<std::optional<int>>` is rejected.

For the same reason, an ordinary `std::optional` cannot directly contain a
smart-pointer null state. For example, `std::optional<std::unique_ptr<int>>`
and `std::optional<std::shared_ptr<int>>` are rejected. In a named map, an
optional field is different: omission represents the empty optional, while a
present key with `null` represents the empty pointer.

One-shot decode is terminal. For a non-null pointer, the decoder creates the
pointee before decoding it. If decoding later returns `incomplete`, the pointer
and any decoded prefix remain in the destination.

## `std::shared_ptr`

Wrap one complete message root with `as_shared_ptrs`:

```cpp
struct message {
    std::shared_ptr<int> first;
    std::shared_ptr<int> second;
};

auto value = std::make_shared<int>(42);
message sent{value, value};

std::vector<std::byte> bytes;
auto enc = make_encoder<shared_ptr_codec>(bytes);
enc(as_shared_ptrs(sent));

message received;
auto dec = make_decoder<shared_ptr_codec>(bytes);
dec(as_shared_ptrs(received));

// Both fields refer to the same decoded int.
assert(received.first.get() == received.second.get());
```

`as_shared_ptrs(root)`:

- writes tag 296 around the root;
- creates a private reference table for that call;
- writes the first non-null pointer as tag 28 around its value;
- writes later occurrences as tag 29 around the earlier table index;
- writes an empty pointer as CBOR `null`;
- destroys the table when the call returns.

For the example above, the relevant shape is:

```text
#6.296([
  #6.28(42),
  #6.29(0)
])
```

A later call starts a new table. It therefore observes the current pointee
value rather than referring to an object from an earlier call:

```cpp
enc(as_shared_ptrs(sent)); // first independent message
*value = 99;
enc(as_shared_ptrs(sent)); // new table; encodes 99
```

There are no public lookup strategies or reserve options. The ordinary wrapper
chooses and owns its internal table.

## Reusing a Caller-Owned Table

Use `as_shared_ptrs_unscoped(root, table)` only when reference indices must
continue across calls:

```cpp
my_encode_table table;

enc(as_shared_ptrs_unscoped(first_segment, table));
enc(as_shared_ptrs_unscoped(second_segment, table));
```

The unscoped form omits tag 296 because the reference namespace is not bounded
by either individual call. The decoder must receive the same segment sequence
with one matching decode table:

```cpp
my_decode_table table;

dec(as_shared_ptrs_unscoped(first_output, table));
dec(as_shared_ptrs_unscoped(second_output, table));
```

The table types are application-defined. An encode table satisfies:

```cpp
expected<shared_ptr_observation, status_code>
observe(const shared_ptr_encode_key&);

void mark_complete(std::uint64_t index);
```

A decode table satisfies:

```cpp
expected<std::uint64_t, status_code>
insert(const shared_ptr_decode_entry&);

expected<shared_ptr_decode_entry, status_code>
resolve(std::uint64_t index);

void mark_complete(std::uint64_t index);
```

These interfaces are checked by `SharedPtrEncodeTable` and
`SharedPtrDecodeTable`. The table chooses its storage, lookup algorithm,
capacity policy, and lifetime.

An encode table must compare `shared_ptr_encode_key::owner` by shared ownership,
not only by `get()`. It must assign indices in first-observation order and
reject:

- a repeated owner with a different `target` (an aliasing pointer);
- a repeated owner with a different `type`;
- a reference to an entry that has not been marked complete (a cycle).

A decode table must preserve insertion order, reject invalid indices, and
return the stored type and completion state unchanged.

External tables provide snapshot semantics. Once a pointee has been observed,
later occurrences are references; mutating the pointee does not send an update:

```cpp
enc(as_shared_ptrs_unscoped(value, table)); // tag 28, sends 42
*value = 99;
enc(as_shared_ptrs_unscoped(value, table)); // tag 29, still refers to snapshot 42
```

Clear or replace the table to begin a new snapshot.

If an unscoped call fails, the codec does not roll the table back or clear it.
The decode destination may also contain a partial result. Treat both as
terminal and discard or explicitly repair the caller-owned state.

## Limits

The shared-pointer codec rejects cycles and aliasing pointers:

```cpp
struct node {
    std::shared_ptr<node> next;
};

auto n = std::make_shared<node>();
n->next = n;
enc(as_shared_ptrs(n)); // status_code::error
```

The first tag 28 entry is installed before its pointee is decoded so partial
state follows the library's one-shot decoder contract. A tag 29 reference to an
unfinished entry is still rejected; cycles are not reconstructed.

The codecs do not prewalk the input or object graph. Input ownership, framing,
admission limits, and the truthful size semantics of the supplied range remain
the caller's responsibility. Parsing the requested CBOR segment and returning
`incomplete` when that segment ends early remain the decoder's responsibility.

## Variants

Pointer alternatives are decoded from their CBOR wire shape, without probing
one alternative and rolling back. Alternatives whose wire shapes overlap are
rejected at compile time:

```cpp
using ok = std::variant<std::unique_ptr<int>, std::string>;

// Rejected: both alternatives accept an integer.
using ambiguous = std::variant<std::unique_ptr<int>, int>;
```

Use an application tag or a different data model when alternatives overlap.

## CDDL

`std::unique_ptr<T>` renders as:

```cddl
T / null
```

Use a schema root that matches the wrapper used on the wire:

```cpp
cddl_schema_to<shared_ptr_cddl<message>>(schema);
cddl_schema_to<shared_ptr_unscoped_cddl<message>>(unscoped_schema);
```

The scoped root includes tag 296:

```cddl
#6.296(null / #6.28(T) / #6.29(uint))
```

The unscoped root omits it:

```cddl
null / #6.28(T) / #6.29(uint)
```

CDDL describes the item shapes. It cannot express whether a tag 29 index
exists, has the correct pointee type, or belongs to a completed entry; those
checks are performed by the decode table.
