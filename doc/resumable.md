# Resumable encoding and decoding

Include `<cbor_tags/cbor_resumable.h>` for C++20 coroutine support. The ordinary
`make_encoder` and `make_decoder` remain synchronous, terminal operations. They
have no coroutine mode or retry checkpoints.

## Supply ordered chunks

The caller owns both chunk descriptors and their bytes. Bind that storage once:

```cpp
namespace ct = cbor::tags;
struct message {
    std::string name;
    std::vector<std::uint32_t> values;
};

std::list<std::span<const std::byte>> chunks;
auto source = ct::make_input_source(chunks);
auto decoder = ct::make_resumable_decoder(source);
message value;
auto op = decoder(value);                 // variadic: decoder(value, checksum)

op(chunk1, chunk2);                       // append the whole batch, then run inline
op(std::span{more_chunks});               // runtime ranges of chunks also work
op(last_chunk, ct::end_of_input);          // explicit EOF; empty chunks are not EOF

auto result = op.result();                // expected<size_t, status_code>
auto tail = op.remaining_input();         // includes an incomplete ordinary item
```

All byte arguments are appended before an EOF token closes the source, regardless
of the token's position in the batch.

`op(...)` returns `resume_state::need_input` or `done`; `op.done()` distinguishes
pending input from a terminal `incomplete` result at EOF. `op.finish()` supplies
EOF without another chunk. The result counts committed bytes for this operation,
including item headers. Cancellation completes with `status_code::cancelled`.
A completed operation ignores further feeds.

Descriptors must be borrowed, common, bidirectional byte ranges: spans and
`std::views::all` of lvalue deque/list byte buffers are examples. Descriptor
storage can be a vector, deque, list, or a prepopulated span. Dynamic feeds need
`push_back`; fixed descriptor storage is supplied before operation construction.
Vector/deque descriptor growth preserves source cursors. Other descriptor
containers must preserve node iterators when appending.

Keep the source, descriptors, and bytes alive until all operations and borrowed
views using them are destroyed. Do not erase descriptors, mutate supplied bytes,
or append directly to descriptor storage after binding it. There is no hidden
input copy or descriptor queue, and no prefix-reclamation API. Source objects are
immovable and permit one active operation. Destroy that operation before starting
the next; the source retains its committed position for subsequent items.

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
auto sink = ct::make_output_source(chunks);
auto encoder = ct::make_resumable_encoder(sink);
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
`op.finish()` closes the sink and makes insufficient space terminal. Encoding a
single-pass, unsized string range retains one bounded payload chunk so its CBOR
chunk header can be emitted before the bytes.

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
factories preserve one-shot group wrapping and integer policy. Structural
traversal reuses reflection, allocators, appenders, header decoding, and variant
mapping. Ordinary primitives are not byte-by-byte coroutine implementations.

Built-in integer arrays drain available input/output in a synchronous loop shared
with ordinary encoding and decoding. Contiguous chunks use the ordinary span
codecs; an integer crossing a chunk boundary uses the chunk-aware codec and is
retried as one item if incomplete. The container awaits only when it needs more
input or output. Custom integer codecs and explicit hooks keep their normal
dispatch.

For definite built-in integer arrays, a reservable destination reserves space
for its existing elements plus the declared count as soon as the complete header
and configured size bounds are validated. Payload bytes need not have arrived;
this also applies to unsized input without prewalking it. The caller owns
allocation limits and can use `as_bounded_size<0, limit>(values)` or a bounded
allocator. Allocation failure returns `status_code::out_of_memory`. Empty arrays
do not reserve, and containers without reservation support grow normally.
One-shot decoding retains its existing availability checks.

## Validation

```cpp
auto op = ct::make_resumable_validator(source);       // one complete CBOR item
// Or: make_resumable_validator(source, 3)
// Or: make_resumable_validator(source, ct::until_end_of_input)
op(chunk1, chunk2, ct::end_of_input);
auto result = op.result();
```

Validation uses the ordinary traversal token decoder and UTF-8 rules, without
constructing application values. `validation_options{.max_depth = 64}` is the
default; pass options as the third factory argument. Incomplete declared payloads
remain `incomplete` at EOF even when their available prefix contains invalid
UTF-8. Indefinite text chunks must each contain complete UTF-8 sequences.

See the [benchmark instructions](../benchmarks/README.md) for throughput and
actual paced-arrival measurements.
