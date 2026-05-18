# Smart Pointer Codecs

Smart pointer support is explicit. Include `cbor_tags/extensions/smart_ptr.h`
and install the codec mixins you want:

```cpp
#include "cbor_tags/cbor_decoder.h"
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/extensions/smart_ptr.h"

#include <memory>

using namespace cbor::tags;
using namespace cbor::tags::ext::smart_ptr;
```

## Nullable Pointers

`nullable_ptr_codec` supports `std::unique_ptr<T>` and `std::shared_ptr<T>` when
`T` is a default-initializable, non-const, non-void, non-array object type.

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
`value` is the pointed-to object. This codec preserves nullable ownership state,
not `shared_ptr` identity.

## Shared Pointer Identity

`shared_graph_codec` preserves repeated `std::shared_ptr<T>` identity inside
explicit `as_shared_graph(...)` roots. The graph state is held in a reusable
session object so identities can span multiple roots in one logical message.

```cpp
std::vector<std::byte> bytes;
auto enc = make_encoder<shared_graph_codec>(bytes);

auto shared = std::make_shared<int>(42);
shared_graph_encode_session encode_graph;

enc(as_shared_graph(encode_graph, shared)); // #6.28(42)
enc(as_shared_graph(encode_graph, shared)); // #6.29(0)

std::shared_ptr<int> first;
std::shared_ptr<int> second;

auto dec = make_decoder<shared_graph_codec>(bytes);
shared_graph_decode_session decode_graph;

dec(as_shared_graph(decode_graph, first));
dec(as_shared_graph(decode_graph, second));
```

Inside a graph session:

- Null `shared_ptr<T>` values encode as `[0]`.
- First-seen non-null values encode as CBOR tag 28, `#6.28(value)`.
- Later references encode as CBOR tag 29, `#6.29(id)`.
- `id` is the zero-based index of the previously decoded tag-28 shareable value
  in that session.

Tags 28 and 29 are the CBOR value-sharing tags registered in the
[IANA CBOR Tags registry](https://www.iana.org/assignments/cbor-tags/cbor-tags.xhtml)
and specified by the linked
[value-sharing tag definition](http://cbor.schmorp.de/value-sharing).

## Session Boundaries

Reuse the same session to share identities across multiple roots in one logical
message:

```cpp
auto shared = std::make_shared<int>(42);
shared_graph_encode_session graph;

enc(as_shared_graph(graph, shared));
enc(as_shared_graph(graph, std::vector{shared, shared}));
```

Call `reset()` between independent messages or streams. Tags 28/29 do not carry
an in-band reset marker, so an encoder reset inside one CBOR stream requires the
decoder to perform the same out-of-band reset at the same boundary.

Graph sessions are stream state, not transactional recovery checkpoints. If an
`as_shared_graph(...)` encode or decode operation fails, discard or reset the
session before continuing with another logical graph. Fine-grained rollback is
reserved for a future checkpoint-oriented API.

## Lookup Policy

Encode-side lookup defaults to `shared_graph_encode_lookup::unordered_map` for
larger graphs. For small graphs or allocation-sensitive scopes, construct the
session with `shared_graph_encode_lookup::linear_scan`:

```cpp
shared_graph_encode_session graph{shared_graph_encode_lookup::linear_scan};
graph.reserve_unique(32);
enc(as_shared_graph(graph, shared));
```

`reserve_unique(n)` reserves space for `n` distinct non-null `shared_ptr`
definitions. It must be called outside active encode operations.

## Composition With Nullable Pointers

`nullable_ptr_codec` and `shared_graph_codec` can be installed together:

```cpp
auto enc = make_encoder<nullable_ptr_codec, shared_graph_codec>(bytes);
auto dec = make_decoder<nullable_ptr_codec, shared_graph_codec>(bytes);
```

Outside `as_shared_graph(...)`, `std::shared_ptr<T>` uses the nullable
`[0]` / `[1, value]` shape only when `nullable_ptr_codec` is installed. Inside
`as_shared_graph(...)`, `std::shared_ptr<T>` uses graph identity encoding.

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

Tag 28, tag 29, and catch-all tag alternatives are ambiguous in graph mode and
fail graph-mode decode:

```cpp
using bad_shareable = std::variant<std::shared_ptr<int>, static_tag<28>>;
using bad_ref       = std::variant<std::shared_ptr<int>, static_tag<29>>;
using bad_catch_all = std::variant<std::shared_ptr<int>, as_tag_any>;
```

The same rejection applies when the colliding tag alternative is nested in
another variant.

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

For aggregate roots the scope applies recursively:

```cpp
struct Root {
    std::shared_ptr<Person> owner;
    std::vector<std::shared_ptr<Person>> reviewers;
};

cddl_schema_to<shared_graph_cddl<Root>>(schema);
```

The generated CDDL describes the wire shape. It cannot prove that a
`#6.29(uint)` reference points to an earlier tag 28 item in the same graph
session; that remains decoder session validation. `std::variant` alternatives
inside `shared_graph_cddl<T>` reject tag 28/29 and catch-all tag collisions, and
also reject array-shaped alternatives that would be ambiguous with the `[0]`
null pointer form.

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
