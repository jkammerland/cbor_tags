# Resumable encoding and decoding

Include `<cbor_tags/cbor_resumable.h>` for C++20 coroutine support. The ordinary
`make_encoder` and `make_decoder` remain synchronous, terminal operations. They
have no coroutine mode or retry checkpoints.

## Supply ordered chunks

The caller owns both chunk descriptors and their bytes. A named decoder keeps
the cursor and EOF state for that storage:

```cpp
namespace ct = cbor::tags;
struct message {
    std::string name;
    std::vector<std::uint32_t> values;
};

std::list<std::span<const std::byte>> chunks;
auto decoder = ct::make_resumable_decoder(chunks);
message value;
auto op = decoder(value);                 // variadic: decoder(value, checksum)

const auto first = op(chunk1);           // append a descriptor, then run inline
assert(first == ct::resume_state::need_input);
const auto last = op(chunk2);             // enough bytes for the requested values
assert(last == ct::resume_state::done);

auto result = op.result();                // expected<size_t, status_code>
assert(result);                           // done also includes terminal errors
assert(value.name == expected_name);
auto tail = op.remaining_input();         // bytes after committed progress
```

Typed decoding finishes when its requested values are available. It does not
need EOF to complete. Feed calls append borrowed chunk descriptors to `chunks`;
they do not copy the payload. Keep calls outside `assert` so they still execute
when assertions are disabled.

`op(...)` returns `resume_state::need_input` or `done`. Inspect `result()` after
completion to distinguish success from an error. The successful result counts
committed bytes for this operation, including item headers. If no more bytes
will arrive, call `op.finish()`: a pending truncated item then completes with
`status_code::incomplete`. Empty chunks do not signal EOF. Cancellation completes
with `status_code::cancelled`. A completed operation ignores further feeds.

### Supply batches

Several chunks can be supplied together, including runtime ranges of chunks:

```cpp
op(chunk1, chunk2);                       // append the whole batch, then run inline
op(std::span{more_chunks});               // runtime ranges of chunks also work
op(last_chunk, ct::end_of_input);          // supply bytes and signal EOF together
```

All byte arguments are appended before an EOF token closes the source, regardless
of the token's position in the batch. `end_of_input` is the batch form of the EOF
signal; `finish()` supplies that signal without another chunk.

Descriptors must be borrowed, common, bidirectional byte ranges: spans and
`std::views::all` of lvalue deque/list byte buffers are examples. Descriptor
storage can be a vector, deque, list, or a prepopulated span. Dynamic feeds need
`push_back`; fixed descriptor storage is supplied before operation construction.
Vector/deque descriptor growth preserves source cursors. Other descriptor
containers must preserve node iterators when appending.

Keep the named factory, descriptors, and bytes alive until all operations and
borrowed views using them are destroyed. The factory stores its adapter directly,
without an additional allocation or shared ownership. It is noncopyable and
immovable because cursors refer back to that adapter. Only a named, non-const
factory can start operations; `make_resumable_decoder(chunks)(value)` is rejected.
Operations themselves remain movable.

Do not erase descriptors, mutate supplied bytes, or append directly to descriptor
storage after binding it. There is no hidden input copy or descriptor queue, and
no prefix-reclamation API. Destroy one operation before starting the next; the
factory retains its committed position, including buffered input after that item.
For example, successive `decoder(first)` and `decoder(second)` operations must
have separate scopes even if the first operation has already completed.

### Explicit sources

Use `make_input_source(chunks)` or `make_output_source(chunks)` when a source needs
to outlive a factory or share its position across different kinds of operations.
Validators and generators continue to take explicit input sources:

```cpp
auto source = ct::make_input_source(chunks);
{
    auto op = ct::make_resumable_decoder(source)(value);
    // Feed or inspect op here; destroy it before starting the next operation.
}
auto validation = ct::make_resumable_validator(source);
```

Factories that take explicit sources borrow those sources, so their existing
temporary-factory syntax remains valid. Keep the explicit source alive until its
operations and views are destroyed. A new source over the same descriptors starts
at the beginning; reuse the original source or named factory to continue a stream.

## Await from another coroutine

```cpp
auto op = decoder(value, checksum);
auto result = co_await op;
if (!result) {
    // Handle result.error().
}
```

A producer holds access to `op` and feeds it from the same serialized execution
context. Feeding resumes internal coroutines and the completed consumer inline.
An executor can post the entire feed call when deferred execution is desired.
There is no scheduler dependency or concurrent-access synchronization.

The operation uniquely owns suspended frames. Moving it preserves a waiting
consumer; destroying the consumer detaches its waiter. A consumer may destroy
or move a completed operation during inline resumption. A pending operation must
outlive its waiter. Cancel before destroying a pending operation when its waiter
must receive a result. Destructive reentrancy from inside a running codec is not
supported.

[The coroutine example](../examples/43_resumable_coroutine.cpp) processes the
decoded message after `co_await`. Its `example::consumer` return type and
`succeeded()` method are example support, not CBOR API: `co_return true` records
that the example's application work succeeded.

For a sequence of top-level items, the generator owns the current operation:

```cpp
std::list<std::span<const std::byte>> chunks;
auto source = ct::make_input_source(chunks);
auto items = ct::make_resumable_generator<message>(source);

// Consumer coroutine:
for (;;) {
    auto result = co_await items.next();
    if (!result) { /* handle error */ break; }
    if (items.done()) break;               // clean EOF, zero-byte result
    consume(items.value());               // valid until the next next() call
}
// Producer: items(chunk1, chunk2); items(ct::end_of_input);
```

`next()` returns a reference to the generator-owned operation; use `auto &next`
when storing it. There is one consumer and one outstanding `next()`. The generator
is immovable while its value and operation can be referenced by suspended code.

## Encode into supplied space

```cpp
std::list<std::span<std::byte>> chunks;
auto encoder = ct::make_resumable_encoder(chunks);
auto op = encoder(value, checksum);

op(output1, output2);                     // need_output or done
op(more_output);
auto result = op.result();
auto bytes = op.produced();               // committed prefix only
```

Ordinary codec output is tentative until the complete item succeeds. Exhausting
space resets the working write position and retries that item when more space is
supplied. Do not publish or reuse tentative bytes. Explicitly resumable strings
commit payload fragments; containers commit completed children. Keep supplied
output storage alive and unmodified through completion, including space written
by an incomplete ordinary item. `remaining_output()` exposes the unwritten range.
`op.finish()` closes the output and makes insufficient space terminal. Encoding a
single-pass, unsized string range retains one bounded payload chunk so its CBOR
chunk header can be emitted before the bytes.

`op.produced()` returns only this operation's committed bytes. For explicit
source users migrating from `output_source::produced(from, to)`, construct
`std::ranges::subrange(from, to)` directly; that redundant source method has been
removed. Its iterators still borrow the source.

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
template <class Decoder>
ct::resumable<ct::status_code> resumable_decode(Decoder &dec, message &value) {
    co_return co_await dec(ct::wrap_as_array{value.name, value.values});
}

template <class Encoder>
ct::resumable<ct::status_code> resumable_encode(Encoder &enc, const message &value) {
    co_return co_await enc(ct::wrap_as_array{value.name, value.values});
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
factories accept either chunk storage or an explicit source and preserve one-shot
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

```cpp
auto op = ct::make_resumable_validator(source);       // one complete CBOR item
// Or: make_resumable_validator(source, 3)
// Or: make_resumable_validator(source, ct::until_end_of_input)
op(chunk1, chunk2, ct::end_of_input);
auto result = op.result();
```

`until_end_of_input` selects validation of every top-level item in the incoming
sequence. Even after the available items are complete, the operation waits for
`finish()` or an `end_of_input` token because another item could arrive.

Validation uses the ordinary traversal token decoder and UTF-8 rules, without
constructing application values. `validation_options{.max_depth = 64}` is the
default; pass options as the third factory argument. Incomplete declared payloads
remain `incomplete` at EOF even when their available prefix contains invalid
UTF-8. Indefinite text chunks must each contain complete UTF-8 sequences.

Within a contiguous input window, scalar siblings share one ordinary decoder
call. Nested items retain their structural continuations, and tokens crossing
input boundaries use the ordinary segmented reader. Completed progress is
published before suspension or operation completion, including error exits.

See the [benchmark instructions](../benchmarks/README.md) for throughput and
paced-arrival commands, methodology, and measurement limitations.
