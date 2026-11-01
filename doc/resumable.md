# Resumable encoding and decoding

Include `<cbor_tags/cbor_resumable.h>` for C++20 resumable operations. Bind the
caller-owned storage once, modify its availability while the codec is suspended,
and call `resume()` without arguments. Encoding, decoding, validation and
sequence generation share this contract.

The ordinary `make_encoder` and `make_decoder` remain synchronous, terminal
operations. They have no coroutine mode or retry checkpoints.

## Decode an ordinary container

```cpp
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cassert>
#include <cstddef>
#include <deque>
#include <string>
#include <vector>

namespace ct = cbor::tags;

int main() {
    const std::string expected = "ready";
    std::vector<std::byte> wire;
    const auto encoded = ct::make_encoder(wire)(expected);
    assert(encoded);

    // The decoder borrows the container and retains its logical position.
    std::deque<std::byte> input;
    auto decoder = ct::make_resumable_decoder(input);
    std::string actual;
    auto operation = decoder(actual);

    input.insert(input.end(), wire.begin(), wire.begin() + 2);
    const auto pending = operation.resume();
    assert(pending == ct::resume_state::need_input);

    input.insert(input.end(), wire.begin() + 2, wire.end());
    const auto completed = operation.resume();
    assert(completed == ct::resume_state::done);
    const auto result = operation.result();
    assert(result);
    assert(*result == wire.size());
    assert(actual == expected);
}
```

Operation creation runs eagerly using currently available storage. A pending
operation reports `need_input` or `need_output`; `resume()` continues it and
returns that state or `done`. Repeated resumption without new availability is
safe, but a driver should wait for a storage change instead of busy-looping.
Calls stay outside assertions so they also execute with assertions disabled.

Typed decoding completes when its requested values are available, without EOF.
`finish()` closes the source and resumes the codec: a truncated item completes
with `status_code::incomplete`. Empty storage does not signal EOF. A completed
operation stays complete on further `resume()` calls. Inspect `result()` to
distinguish successful completion from a terminal error.

## Encode into a growable container

```cpp
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cassert>
#include <cstddef>
#include <deque>
#include <ranges>
#include <string>

namespace ct = cbor::tags;

int main() {
    const std::string expected = "ready for transport";
    std::deque<std::byte> output;
    auto encoder = ct::make_resumable_encoder(output);
    auto operation = encoder(expected);
    assert(operation.done());

    // A growable container appends normally; no output buffers are passed to resume.
    const auto result = operation.result();
    assert(result);
    auto committed = output | std::views::take(operation.committed_bytes());
    auto decoder = ct::make_decoder(committed);
    std::string actual;
    const auto decoded = decoder(actual);
    assert(decoded);
    assert(actual == expected);
}
```

Growable vector/deque output appends after existing contents. Fixed byte storage
and nested writable ranges provide bounded capacity; exhaustion suspends the
operation with `need_output`. The caller makes additional storage available
through the bound container or adapter, then calls `resume()`.

`committed_bytes()` counts this operation's committed bytes, including headers.
It remains available while pending and after success, errors or cancellation.
Only committed bytes may be published. Storage size alone is not a general
encoded-length indicator: fixed storage contains spare capacity and an
incomplete ordinary codec can leave tentative writes.

## Storage and mutation

Default adapters support ordinary byte ranges and nested byte ranges, including
containers of owning containers. For flat vector/deque input, append bytes while
preserving the existing prefix. For nested storage, append descriptors or grow
the final inner range while preserving earlier contents and order. Final-range
growth is supported for sized ranges or unsized ranges whose end boundary
changes. An unsized node view with an unchanged end sentinel needs a custom
source to expose growth without a second traversal. Unsized input is not
prewalked to calculate a byte extent.

Indexable storage permits reallocation between resumptions: the adapter retains
logical positions instead of persistent raw iterators. Node-based storage must
preserve existing element iterators. An indexed outer container must also
preserve iterators into node-based inner ranges when it relocates them. Reserve
descriptor capacity before binding the source if relocation copies owning node
ranges, as can happen with `vector<list<byte>>`. Unsized inner ranges must also
keep a previously observed end iterator valid for comparison after permitted mutations.
Use a custom source when an unsized range cannot preserve that boundary.

Removing descriptors, shrinking the tail, replacing an earlier range, or adding
storage after closure violates the default adapter's caller
preconditions. Debug builds assert locally detectable violations; applications
must satisfy the contract in every build. Malformed CBOR, arithmetic overflow
and attempts to start a second operation on an acquired source remain checked
errors.

Inner ranges may own their bytes, as in `vector<vector<byte>>`, or borrow them
through spans and views. The outer container can be a vector, deque, list or
prepopulated span. Empty inner ranges are allowed. All containers and backing
bytes remain caller-owned. Prefix removal and recycling require a custom source
with an explicit reclamation policy.

Keep the named factory, storage and encoded values alive through every operation
that borrows them. A storage-bound factory owns its adapter inline, is immovable,
and starts operations only as an lvalue. Destroy an operation before starting
another on the same source; completion alone does not release the source.
Successive operations retain the committed stream position and buffered tail.

Use `make_input_source(storage)` or `make_output_source(storage)` when a source
must outlive a factory or share its position between operation kinds. Factories
borrow explicit sources, so temporary-factory construction remains valid.
Validators and generators take explicit input sources.

## Public custom sources and reclamation

`<cbor_tags/cbor_resumable_source.h>` exposes `ResumableInputSource` and
`ResumableOutputSource`. Custom sources implement public members, without
specializing implementation details. The ordinary reader/appender bridge stays
inside the library. Custom sources expose noncontiguous logical traversal, even
when their backing allocation is contiguous; optional windows provide bulk
access. Default adapters still accept ordinary contiguous containers. Declaring
`position_type` identifies a custom source: it must satisfy the public source
contract and cannot fall back to being adapted as an ordinary container.

The source contract includes:

- Copyable durable positions exposing monotonic absolute byte offsets as exactly
  `std::size_t`. Source byte counts, sized input range sizes and any `size_type`
  also use `std::size_t`; output sources expose `value_type = std::byte`.
  Positions at and after commitment survive the source's documented mutations.
- Working and committed positions, with rollback restricted to uncommitted work.
  Input supplies byte traversal, `commit(position)` and `remaining(position)`;
  output supplies `position()`, `commit()`, `restore(position)` and
  `remaining_output()`.
- Nonthrowing `committed_position()`, `release()` and output `restore(position)`
  so abandoning an operation can safely discard tentative work.
- A required `refresh()` method, called before codec driving. It may do nothing
  when durable positions already reflect availability. `eof()`/`close()` mark
  terminal availability; `acquire()`/`release()` enforce one active operation.
- Output `write(span<const byte>)` returning the number of bytes written. A short
  write denotes temporary exhaustion, not successful completion of the request.
- Optional contiguous borrow/consume windows for bulk transfer. Byte traversal
  remains available for noncontiguous containers.

A sized range must report its actual retained byte count. Sized sources provide
`remaining_size(position)` for exact availability at that position, including a
nonzero retained base; absolute stream offsets must not be compared directly
against a retained range's size. Unsized sources need no total extent.

The codec retains no cumulative output view. An output adapter uses
`committed_position()` to publish newly committed ranges exactly once. Input
and output may reclaim bytes strictly before commitment only when all external
borrows permit it. Publication, pending I/O ownership and final reclamation are
separate adapter/application responsibilities.

Mutation and codec driving are serialized. Temporary codec windows end before
suspension; positions must remain valid through permitted mutations, either
directly or by reacquiring access when needed. A decoded `span` or `string_view`
has a longer application borrow and requires its backing
allocation to remain stable. Such views cannot represent bytes crossing separate
allocations; use an owning result, an appropriate segmented value or the normal
view decoder's unsupported-input outcome.

Bounded storage must accommodate an ordinary codec's entire retry unit. Use an
explicit resumable customization for larger values, or reject them under the
application's capacity policy. Waiting cannot free tentative bytes required by
the current retry.

The complete [ring source example](../examples/46_resumable_ring.cpp) implements
both public source concepts using only standard-library storage and logical
absolute positions. It copies committed output to a transport buffer before
reclaiming it, feeds input in bounded chunks, and wraps the same fixed storage
throughout each operation. Its `refresh()` is empty because availability is
read directly from the source.

## Awaiting, cancellation and sequences

`co_await operation` observes terminal completion; it does not supply storage or
drive the codec. A producer modifies the bound storage and calls `resume()` from
the same serialized execution context. Completion resumes its waiter inline.
An executor may post the whole resumption when deferred execution is desired.

The operation uniquely owns suspended frames and remains movable. Moving it
preserves a waiting consumer; destroying the consumer detaches its waiter. A
consumer may destroy or move a completed operation during inline resumption.
A pending operation must outlive its waiter. `cancel()` rolls back tentative
cursor progress and completes with `status_code::cancelled`, waking the waiter
inline. Cancel before destroying a pending operation when its waiter needs a
result. Destructive reentrancy from inside a running codec is unsupported.

`make_resumable_generator<T>(source)` retains a current value and operation.
`next()` returns a reference to that operation; copy the value before requesting
another. The producer calls argument-free `resume()` or `finish()` after changing
storage. Call `next()` first, keep one outstanding request, and keep the generator
alive while its operation/value is borrowed. Clean EOF returns a successful
zero-byte result and sets `done()`.

See the complete [coroutine example](../examples/43_resumable_coroutine.cpp) and
[generator example](../examples/44_resumable_generator.cpp). Their small coroutine
owner is example support, not part of the CBOR API.

## Codec opt-in

Short built-in values and ordinary custom codecs run through the normal codec.
An incomplete call resets only its input/output cursor. It does **not** restore
the destination, codec members, callbacks, or external side effects. An ordinary
custom decoder must publish destination changes only after its entire item
succeeds, or explicitly opt into resumability. This contract also applies to
extension codecs: an ordinary codec that appends before returning `incomplete`
is not automatically safe to retry.

Use a named hook to retain progress through long values:

```cpp
#include <cbor_tags/cbor_resumable.h>
#include <cassert>
#include <cstddef>
#include <string>
#include <vector>

namespace ct = cbor::tags;

struct message {
    std::string name;
    std::vector<int> values;
};

template <class Decoder>
ct::resumable<ct::status_code> resumable_decode(Decoder &dec, message &value) {
    co_return co_await dec(ct::wrap_as_array{value.name, value.values});
}

template <class Encoder>
ct::resumable<ct::status_code> resumable_encode(Encoder &enc, const message &value) {
    co_return co_await enc(ct::wrap_as_array{value.name, value.values});
}

int main() {
    const message expected{"station", {1, 24, 1000}};
    std::vector<std::byte> storage;
    auto encoder = ct::make_resumable_encoder(storage);
    auto encoding = encoder(expected);
    assert(encoding.done());
    assert(encoding.result());

    auto decoder = ct::make_resumable_decoder(storage);
    message actual;
    auto decoding = decoder(actual);
    assert(decoding.done());
    assert(decoding.result());
    assert(actual.name == expected.name);
    assert(actual.values == expected.values);
}
```

Hooks can be members (private members may friend `ct::Access`), ADL functions, or
codec mixin methods. `resumable_transcode` is the symmetric alternative. Return
exactly `resumable<status_code>` and compose through the variadic `operator()`.
A ready result, `return ct::status_code::success;`, needs no coroutine frame.
Member/ADL hooks use the ordinary automatic metadata-tag convention. Mixin hooks
own the complete wire shape, including tags. Explicit hooks take precedence over
ordinary customizations; a generic ordinary `decode(auto&)` does not opt in.

Within a variant, an explicit hook may reject an alternative before committing
bytes and allow a simple-value catchall to run. Rejection after committed
progress is terminal; committed input is never rewound to try another alternative.

`make_resumable_decoder<Codec...>` and `make_resumable_encoder<Codec...>` install
mixins. The corresponding `make_resumable_*_with_options<Options, Codec...>`
factories accept either supported byte storage or an explicit source and preserve one-shot
group wrapping and integer policy. Structural
traversal reuses reflection, allocators, appenders, header decoding, and variant
mapping. Ordinary primitives are not byte-by-byte coroutine implementations.

Arrays of built-in integers, `float`, `double`, and `bool` drain available
input/output in a synchronous loop shared with ordinary encoding and decoding.
Contiguous chunks use the ordinary span codecs with the supported wire-size
bound for their element type. An element crossing a chunk boundary uses the
chunk-aware codec and is retried as one item if incomplete. The container awaits
only when it needs more input or output. Custom codecs and explicit hooks keep
their normal dispatch; proxy references retain the generic encoding path.

Indefinite arrays of these types use the same approach, sharing ordinary
header-context decoding and break handling. A break can finish an array at its
configured size limit; reaching end of input without a break remains incomplete.

Maps whose keys and values use these built-in codecs also share synchronous
ordinary codec loops. Each completed key commits independently, so a value split
across chunks resumes without decoding or encoding its key again. Definite and indefinite maps count
wire pairs, including duplicate keys, and append only complete pairs.

For definite strings, arrays, and maps whose destination appender supports
reservation, structural decoding reserves space for existing contents plus the
declared size as soon as the complete header and configured size bounds are
validated. Payload bytes need not have arrived; this also applies to unsized
input without prewalking it. For maps, the declared size counts key/value pairs,
including duplicate keys; reserving an unordered map prepares its buckets but
does not allocate its elements. Fixed destinations retain their extent checks.

The caller owns allocation limits and can use `as_bounded_size<0, limit>(values)`
or a bounded allocator. Allocation failure returns `status_code::out_of_memory`.
Empty items and indefinite outer headers do not reserve. For indefinite strings,
each validated definite child can reserve its declared payload before it arrives;
destinations exposing capacity retain geometric growth across small children.
Payloads already available transfer synchronously without a child coroutine frame.
Containers without reservation support grow normally. One-shot decoding retains
its existing availability checks.

With `codec::typed_array`, owning RFC 8746 arrays resume decoding directly into
their numeric storage. Encoding supports owning arrays and borrowed range
wrappers. Both paths reuse ordinary tag/header handling and endian conversion;
only one element crossing a transport boundary needs fixed scratch storage.
The header and completed elements commit independently. An incomplete element
remains uncommitted at EOF, and following items remain available as input tail.
The codec continues to require a definite byte-string payload, which may arrive
in any number of transport chunks. Borrowed decode views retain ordinary
fallback behavior and their input-lifetime requirements.

## Validation

Validators use an explicit input source and the same `resume()`/`finish()`
contract. `make_resumable_validator(source)` validates one item; a numeric
second argument selects a fixed number of items. `ct::until_end_of_input`
selects every top-level item until explicit closure. It keeps waiting after the
available items are complete because another item could arrive.

Validation uses the ordinary traversal token decoder and UTF-8 rules, without
constructing application values. `validation_options{.max_depth = 64}` is the
default; pass options as the third factory argument. Incomplete declared payloads
remain `incomplete` at EOF even when their available prefix contains invalid
UTF-8. Indefinite text chunks must each contain complete UTF-8 sequences.

Within a contiguous input window, scalar siblings share one ordinary decoder
call. Nested items retain their structural continuations, and tokens crossing
input boundaries use the ordinary segmented reader. Completed progress is
published before suspension or operation completion, including error exits.

See the [validation example](../examples/42_resumable_validation.cpp) and the
[benchmark instructions](../benchmarks/README.md) for executable usage and
measurement commands.
