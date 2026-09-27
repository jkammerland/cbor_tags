# Item traversal and validation

Use `validate_item` to check the next complete CBOR item. Success leaves any
following item unread.

```cpp
#include <cbor_tags/cbor_traversal.h>

namespace ct = cbor::tags;
auto dec = ct::make_decoder(buffer);
auto result = ct::validate_item(dec);
```

Validation checks structure, UTF-8 in each text chunk, and simple-value
encodings. Schema, tag semantics, map-key uniqueness, and deterministic
encoding require additional checks.

## Visit an item

`walk_item` delivers headers, scalar values, and borrowed payloads to one
visitor. For example, count arrays in an item:

```cpp
auto dec = ct::make_decoder(buffer);
std::size_t arrays = 0;
auto result = ct::walk_item(dec, [&](const auto& value, const auto& context) {
    using T = std::remove_cvref_t<decltype(value)>;
    if constexpr (std::same_as<T, ct::as_array_any>) {
        if (context.kind == ct::walk_event_kind::enter) {
            ++arrays;
        }
    }
});
```

The visitor must accept every event type. Return `void`, or return
`status_code::success` to continue and another status to fail the walk.
Success means the entire item was consumed.

| `context.kind` | Value | `context.source` |
| --- | --- | --- |
| `enter` | Array, map, string, or tag header | Encoded header |
| `value` | Scalar | Complete encoding |
| `value` | Borrowed text or byte view | Payload bytes |
| `leave` | Original header | Closing break or empty range |

The callback argument's type distinguishes a scalar from borrowed string
contents. For `[42, "hi"]`, the callbacks are:

```text
enter    array(size=2)
  value    42
  enter    text(size=2)
  value    "hi"
  leave    text(size=2)
leave    array(size=2)
```

The integer gets one callback. The string gets three because its header and
borrowed contents are delivered separately. An indefinite string adds an outer
boundary around its definite chunks:

```text
enter    indefinite text
  enter    text chunk
  value    chunk contents
  leave    text chunk
  ...more chunks...
leave    indefinite text
```

Byte strings follow the same pattern. Successful walks pair every `enter` with
a `leave`, including empty items with headers. Definite strings produce a
`value` event even when empty; an indefinite string with no chunks does not.
The walker consumes closing breaks internally.
`context.depth` starts at zero and increases for child items and chunks;
string-content and leave events use their header's depth.

Walking checks structure. Add strict text and simple-value validation with
`ct::walk_item(dec, visitor, {.strict_validation = true})`. Invalid payloads
are rejected before their value callback; a header callback may
already have run.

## Lifetime, limits, and errors

- Event values and contexts last for the callback. Payload views and source
  ranges borrow the input: keep it stable and do not advance the decoder from
  the visitor.
- Both helpers default to `max_depth = 64`. A header at that depth fails;
  arrays, maps, tags, strings, and string chunks count. Scalar leaves do not.
- Both return `ct::expected<void, ct::status_code>`. Truncation returns
  `incomplete`, invalid UTF-8 returns `invalid_utf8_sequence`, and the depth
  limit returns `size_limit_exceeded`. Visitor failure statuses propagate.
- Failure is terminal: consumed input and callback effects remain. Failed
  items may have no matching leave event.
