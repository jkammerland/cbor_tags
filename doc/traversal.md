# Item traversal and validation

Include `<cbor_tags/cbor_traversal.h>` to traverse or validate the next complete
CBOR item without constructing a decoded object tree. Both helpers use the
existing decoder, header descriptors, and borrowed payload readers.

```cpp
namespace ct = cbor::tags;

auto dec = ct::make_decoder(buffer);
auto result = ct::validate_item(dec);
if (!result) {
    // result.error() is a ct::status_code.
}
```

A successful call consumes exactly one item. It leaves following input for the
next call; it does not establish that the entire input contains exactly one
item. Empty input returns `status_code::incomplete`.

## Visiting an item

`walk_item(decoder, visitor, options)` hides the token variant and structural
traversal. The visitor is one callable receiving a value and a context:

```cpp
std::size_t arrays = 0;
auto result = ct::walk_item(dec,
    [&](const auto& value, const auto& context) {
        using T = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<T, ct::as_array_any>) {
            if (context.phase == ct::walk_phase::begin) {
                ++arrays;
            }
        }
    });
```

The callable must accept all event value types. A generic lambda can ignore
uninteresting types with `if constexpr`. It may return `void`, or return
`status_code::success` to continue and another `status_code` to fail the walk.
Other return types are rejected at compile time. There is no successful early
stop or subtree-skip result: success means the root item was fully consumed.

| Phase | Value | Source range |
| --- | --- | --- |
| `begin` | An existing array, map, string, or tag header descriptor | Its encoded header |
| `value` | A scalar: `positive`, `negative`, a floating-point type, `bool`, `nullptr_t`, or `simple` | Its complete encoding |
| `payload` | An existing borrowed text or byte payload view | Its payload bytes |
| `end` | The original header descriptor | Its closing break, or an empty range at the cursor |

Every successful header visit has matching `begin` and `end` events, including
definite strings, empty containers, and tags. A definite string also produces a
payload event when empty. An indefinite string exposes its definite chunks
individually, each with its own begin/payload/end events. Chunks are not
concatenated.

The walker consumes `indefinite_break` internally. Both definite and indefinite
containers produce end events; a caller can handle their logical completion
without looking for a wire delimiter.

`walk_context<Iterator>` supplies `phase`, `depth`, and a `source` subrange of
the original input. The root has depth zero. Child items and string chunks
have depth one greater than their parent. Payload and end events retain their
owning header's depth. Source ranges preserve actual bytes, including
nonminimal argument encodings.

Event values and contexts are temporary. Copy values that must outlive the
callback. Payload views and source iterators borrow the caller's input and
remain subject to its lifetime and iterator-invalidation rules. The caller
must keep that input stable and must not advance the same decoder from a
callback.

## Validation policies

Ordinary walking checks structure using the library's existing permissive
decoding profile. It verifies required children and payloads, map key/value
pairing, legal breaks, and definite string chunks of the matching major type.

Enable additional checks while visiting:

```cpp
auto result = ct::walk_item(dec, visitor, {.strict_validation = true});
```

`validate_item(dec, validation_options)` uses that same strict traversal with a
no-output visitor. Strict validation additionally:

- rejects a two-byte simple-value encoding whose argument is below 32;
- requires valid UTF-8 for each definite text payload, including each chunk of
  an indefinite text string.

These checks cover syntax and UTF-8. They do not establish canonical or
deterministic encoding, unique map keys, a schema, or tag-specific semantic
validity. Nonminimal but well-formed argument encodings are accepted. The
ordinary typed decoder retains its documented permissive behavior.

Invalid simple encodings are rejected before their value callback. Text is
checked after its payload is available and before the payload callback. Its
header callback may already have run.

## Limits and failures

Both option types default to `max_depth = 64`. A header at depth greater than
or equal to that limit is rejected. Arrays, maps, tags, and string headers
count toward the limit; scalar leaves, payloads, and closing delimiters do not
add levels. Thus a limit of zero permits scalar roots only, while a limit of
one permits an array of scalar values but rejects a nested container. String
chunks count as nested string headers.

Both functions return `ct::expected<void, ct::status_code>` using the configured
expected backend, independently of a decoder's custom return-value option.

| Failure | Status |
| --- | --- |
| Missing or truncated input | `incomplete` |
| Invalid structure or strict simple-value encoding | `error` |
| Invalid text in strict mode | `invalid_utf8_sequence` |
| Depth limit | `size_limit_exceeded` |
| Allocation or length exception | `out_of_memory` |
| Visitor returns a failure status | That same status |
| Other exception | `error` |

Incomplete-read exceptions map to `incomplete`. Failure is terminal: consumed
input and earlier callback effects remain, and there is no rewind or retry.
Failed items need not produce matching end events.

Contiguous, sized noncontiguous, and unsized bidirectional input ranges use the
decoder's existing availability checks. The walker does not prewalk input or
copy payloads. Accessing a borrowed noncontiguous payload in a callback or for
UTF-8 validation can traverse that view after the decoder has consumed it;
this is payload processing, not a speculative availability scan.
