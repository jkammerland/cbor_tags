# Smart Pointer Codecs

Smart pointer support is explicit. Include `cbor_tags/extensions/smart_ptr.h`
and choose how pointers should be stored:

```cpp
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <memory>

using namespace cbor::tags;
using namespace cbor::tags::ext::smart_ptr;
```

## Why There Are Two Codecs

The codecs solve different problems:

| If you need... | Use | Meaning |
|---|---|---|
| A pointer that may be empty | `nullable_ptr_codec` | “Nullable” means it may be `nullptr`. |
| Several `std::shared_ptr`s to keep pointing to one object after decoding | `shared_graph_codec` | “Shared” means one object has more than one pointer. |

Use the first codec unless keeping that “same object” relationship matters.

## Pointers That May Be Empty

`nullable_ptr_codec` stores whether a `std::unique_ptr<T>` or
`std::shared_ptr<T>` is empty. It supports ordinary object types that the
decoder can create with `T{}`.

```cpp
std::vector<std::byte> bytes;
auto enc = make_encoder<nullable_ptr_codec>(bytes);

std::unique_ptr<int> value = std::make_unique<int>(42);
enc(value);

std::unique_ptr<int> decoded;
auto dec = make_decoder<nullable_ptr_codec>(bytes);
dec(decoded);
```

Wire shape:

```cddl
nullable<T> = [0] / [1, T]
```

Null pointers encode as `[0]`. Non-null pointers encode as `[1, value]`, where
`value` is the pointed-to object.

If the same `shared_ptr` appears twice, this codec writes two values. After
decoding they have the same contents, but they are two separate objects. Use
`shared_graph_codec` when both pointers must still point to one object.

## Keep Repeated `shared_ptr`s Pointing To One Object

`shared_graph_codec` is for that second case. Wrap the value in
`as_shared_graph(...)` so the library knows to keep track of repeated
`std::shared_ptr`s. The classes named `...session` are small “already seen”
lists: one for writing and one for reading.

```cpp
std::vector<std::byte> bytes;

auto item = std::make_shared<int>(42);
std::vector<std::shared_ptr<int>> sent{item, item};

shared_graph_encode_session write_state;
auto enc = make_encoder<shared_graph_codec>(bytes);
enc(as_shared_graph(write_state, sent));

std::vector<std::shared_ptr<int>> received;
auto dec = make_decoder<shared_graph_codec>(bytes);
shared_graph_decode_session read_state;
dec(as_shared_graph(read_state, received));

// received[0].get() == received[1].get()
```

While one message is being written or read:

- Null `shared_ptr<T>` values encode as `[0]`.
- The first pointer to an object uses CBOR tag 28, `#6.28(value)`.
- A later pointer to that object uses CBOR tag 29, `#6.29(id)`.
- `id` is the number assigned to the first object in that tracker.

Tags 28 and 29 are the CBOR value-sharing tags registered in the
[IANA CBOR Tags registry](https://www.iana.org/assignments/cbor-tags/cbor-tags.xhtml)
and specified by the linked
[value-sharing tag definition](http://cbor.schmorp.de/value-sharing).

## One Tracker Per Message

Reuse the same tracker if one message is written or read as several CBOR values:

```cpp
auto shared = std::make_shared<int>(42);
shared_graph_encode_session graph;

enc(as_shared_graph(graph, shared));
enc(as_shared_graph(graph, std::vector{shared, shared}));
```

Call `reset()` or make a new tracker for the next independent message. CBOR does
not write a reset marker, so the writer and reader must agree on that message
boundary.

If `as_shared_graph(...)` fails, discard or reset the tracker before using it
again. It may already contain part of the failed message.

## Choosing The Tracker

The default is suitable for most messages. For a small message or when you need
to control memory use, choose `shared_graph_encode_lookup::linear_scan`:

```cpp
shared_graph_encode_session graph{shared_graph_encode_lookup::linear_scan};
graph.reserve_unique(32);
enc(as_shared_graph(graph, shared));
```

`reserve_unique(n)` prepares room for `n` different non-empty `shared_ptr`s.
Call it before encoding.

## Using Both Codecs

`nullable_ptr_codec` and `shared_graph_codec` can be installed together:

```cpp
auto enc = make_encoder<nullable_ptr_codec, shared_graph_codec>(bytes);
auto dec = make_decoder<nullable_ptr_codec, shared_graph_codec>(bytes);
```

Install both only when a message has both kinds of use. Outside
`as_shared_graph(...)`, `std::shared_ptr<T>` uses the simple empty-or-value
form. Inside `as_shared_graph(...)`, repeated pointers keep pointing to the
same object.

## Variants

Nullable smart pointer alternatives use the array shape `[0]` / `[1, value]`.
For that reason, variant support rejects ambiguous shapes at compile time:

```cpp
// Error: both alternatives use the nullable pointer shape.
std::variant<std::unique_ptr<int>, std::shared_ptr<int>> value;

// Error: the pointer alternative collides with another array-shaped alternative.
std::variant<std::shared_ptr<int>, std::vector<int>> other;
```

In graph wrappers, `std::shared_ptr<T>` contributes virtual tag alternatives 28
and 29. Non-colliding static tags can coexist:

```cpp
using ok = std::variant<std::shared_ptr<int>, static_tag<42>, std::string>;
```

Graph-mode variants can also dispatch a direct vector of shared pointers by its
top-level array shape:

```cpp
using ok_vector = std::variant<std::vector<std::shared_ptr<int>>, std::string>;
```

Tag 28, tag 29, and catch-all tag alternatives are ambiguous in graph mode and
fail graph-mode decode when a direct `std::shared_ptr<T>` alternative is present:

```cpp
using bad_shareable = std::variant<std::shared_ptr<int>, static_tag<28>>;
using bad_ref       = std::variant<std::shared_ptr<int>, static_tag<29>>;
using bad_catch_all = std::variant<std::shared_ptr<int>, as_tag_any>;
```

The same rejection applies when the colliding tag alternative is nested in
another variant. A graph vector alternative is also rejected if any other
alternative is array-shaped, and broader indirect pointer forms are not variant
dispatch targets:

```cpp
using bad_array = std::variant<std::vector<std::shared_ptr<int>>, std::vector<int>>;
using bad_nested = std::variant<std::optional<std::shared_ptr<int>>, std::string>;
```

## CDDL

CDDL generation renders nullable pointer shapes as `[0] / [1, T]`, matching
`nullable_ptr_codec`.

```cpp
#include "cbor_tags/extensions/cbor_visualization.h"
#include "cbor_tags/extensions/smart_ptr.h"

std::string schema;
cddl_schema_to<std::shared_ptr<int>>(schema);
// root = [0] / [1, int]
```

Use `shared_graph_cddl<T>` when the schema should describe values encoded
through `as_shared_graph(...)`. In that scoped schema, `std::shared_ptr<T>`
renders as the graph wire shape: null pointer, first shareable value, or later
shared reference.

```cpp
std::string schema;
cddl_schema_to<shared_graph_cddl<std::shared_ptr<int>>>(schema);
// root = [0] / #6.28(int) / #6.29(uint)
```

The wrapper is only valid at the schema root. For aggregate roots the scope
applies recursively:

```cpp
struct Root {
    std::shared_ptr<Person> owner;
    std::vector<std::shared_ptr<Person>> reviewers;
};

cddl_schema_to<shared_graph_cddl<Root>>(schema);
// Root = [[0] / #6.28(Person) / #6.29(uint), [* ([0] / #6.28(Person) / #6.29(uint))]]
// Person = ...
```

The generated CDDL describes the wire shape. It cannot prove that a
`#6.29(uint)` reference points to an earlier tag 28 item in the same graph
session; that remains decoder session validation. `std::variant` alternatives
inside `shared_graph_cddl<T>` reject tag 28/29 and catch-all tag collisions when
a direct `std::shared_ptr<T>` alternative is present. A direct
`std::vector<std::shared_ptr<T>>` alternative is supported when no other
alternative is array-shaped.

## Limits

`shared_graph_codec` is an acyclic shared-reference codec. Cycles are rejected:

```cpp
struct Node {
    std::shared_ptr<Node> next;
};

auto n = std::make_shared<Node>();
n->next = n;

enc(as_shared_graph(graph, n)); // error: cycles unsupported
```

Graph identity is keyed by `shared_ptr::get()` and one static pointer type per
object. Cross-static-type identity, aliasing-pointer identity, and cycles are
not supported.
