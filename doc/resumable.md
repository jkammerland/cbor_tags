# Resumable encoding, decoding and validation

Include `<cbor_tags/cbor_resumable.h>` and use `cbor::tags`. These opt-in operations
retain C++20 coroutine state between byte chunks. A feed call resumes work inline;
there is no scheduler, transport, background task, or separate resume call.
The ordinary one-shot encoder and decoder keep their existing contracts.

## Decode ordered input

```cpp
int number{};
std::string text;
auto dec = make_resumable_decoder();
auto op = dec(number, text);

// first/last are byte ranges; batch is a finite range of byte ranges.
auto progress = op(first, batch, last, end_of_input);
if (op.done()) {
    expected<std::size_t, status_code> result = op.result();
    // Success contains the total bytes consumed by this operation.
}
```

The factory call selects values in order, including supported aggregate structs,
containers and wrappers. Each operation is move-only. Decoder destinations are
borrowed lvalues; keep them alive throughout the operation. Temporary wrappers
such as `wrap_as_array{number, text}` may refer to those stable lvalues.
Options and codecs use `make_resumable_decoder_with_options<Options, Codecs...>()`
or `make_resumable_decoder<Codecs...>()`.

Input chunks are forward ranges of supported byte types. They may be contiguous
spans, arrays, vectors, deques or lists. An outer batch need only be an
`input_range`; it is enumerated once, in order, to capture its chunk descriptors
before decoding starts. It must be finite. Each chunk's backing must remain
stable as that enumeration advances: a batch cannot repeatedly expose one buffer
and overwrite its bytes on increment.

Descriptors are copied; byte storage remains borrowed. A temporary span or
borrowed subrange is safe when its underlying storage survives. A temporary
owning byte container is rejected. A temporary batch of spans can be captured,
but its payload storage still belongs to the caller. Keep unread bytes, yielded
payload bytes, and decoded borrowed views alive while any consumer uses them.
Owning text/byte destinations can span chunks. A contiguous borrowed view needs
its complete payload in one contiguous chunk; split storage is not coalesced.

`progress` contains the state, bytes consumed during this call, accepted chunk
count, and error status. Its byte count includes buffered items drained by a
consumer resumed inline during the call. States are `need_input`, `need_output`,
`yielded`, `done`, and `failed`. Call `result()` only after `done()` is true.
The result is `expected<std::size_t, status_code>`, including on failure.

`op.remaining_input()` borrows the ordered unread chunk suffix, including the
remaining cursor within the current chunk. It may include exhausted descriptors;
skip empty chunks when needed. Copy descriptors before the next feed if you need
to retain this suffix, and preserve their backing bytes. Successful typed decode
stops after its requested values; it does not validate arbitrary trailing input.

Empty chunks do not mean EOF. A partial item remains pending until another feed
or the explicit `end_of_input` marker, which must be the final feed argument.
EOF inside an item returns `incomplete`. A completed operation, or one already
given EOF, accepts no further input. `cancel()` makes pending work terminal with
`cancelled`; destroying an unfinished operation also cancels it. Neither action
cancels application-owned I/O.

## Await completion or consume items

An existing coroutine can use the standard raw coroutine awaitable protocol:

```cpp
auto result = co_await op; // expected<size_t, status_code>
```

The application supplies input elsewhere through `op(chunks...)`. Completion
resumes the waiting coroutine before that feed returns. Already-completed work
continues inline without registering a waiter. Keep the consumer frame alive
until delivery, or destroy its suspended awaiter to detach it before later feeds.
The application owns scheduling and any deferred executor handoff.

Generators expose one value at a time:

| Factory | Values returned by `value()` | Completion boundary |
| --- | --- | --- |
| `dec.items<T>()` | Complete `T` items from a CBOR sequence | Explicit EOF between items |
| `dec.elements<T>()` | Elements of one definite or indefinite CBOR array | Array end |
| `dec.bstr()` | Borrowed byte-string payload ranges | String end |
| `dec.tstr()` | Borrowed text-string payload byte ranges | String end |

`T` must describe one complete CBOR item. An aggregate with its normal multi-field
array encoding can be an item; an unwrapped group is not a sequence element.
String generators omit headers and breaks; their fragments can follow transport
splits and need not match CBOR's indefinite-string chunk boundaries.
`tstr()` streams payload bytes; it does not perform strict UTF-8 validation.

```cpp
auto values = dec.items<int>();
// In an application coroutine, with input supplied through values(chunks...):
while (co_await values.next()) {
    consume(values.value());
}
auto result = values.result();
```

A ready `next()` continues inline; a pending request waits for input. Requesting
the next value acknowledges the previous yield and may immediately parse more
buffered bytes. `value()` is valid after a successful next request until that
acknowledgment or cancellation. Copy data you need before requesting another
item. Payload `readable_chunk` values are forward ranges; `try_span()` returns an
optional contiguous view when available.

Even the last yield must be acknowledged by requesting `next()` again before
`done()` and `result()` describe terminal completion. Breaking the loop does not
finish the operation. Only one result/next waiter may be active. Serialize feeds,
requests, cancellation and destruction; concurrent access is unsupported. A
consumer resumed inline may request more buffered items, but recursively feeding
the same operation before its current feed returns is rejected.

An incoming async chunk generator is separate from this decoded-item generator.
The application's coroutine awaits its chunks and passes each one to `op(chunk)`.
Do not advance a source that reuses its backing buffer until the decoder no
longer retains unread bytes or borrowed payloads from it.

## Validate without constructing values

```cpp
auto one = make_resumable_validator();       // One complete item.
auto pair = make_resumable_validator(2);     // Two complete items.
auto stream = make_resumable_validator(until_eof);
auto limited = make_resumable_validator(1, validation_options{.max_depth = 16});
```

Feed these operations exactly like a decoder, or await their result. Counted
validation stops after the requested items and leaves trailing input untouched.
`until_eof` validates the entire supplied sequence and needs explicit EOF.
Validation checks CBOR structure and strict UTF-8, including individual chunks of
indefinite text strings. Its traversal depth defaults to 64 through
`validation_options`; this is a validator limit, not a global typed-decoder
recursion or transport admission policy. Validation does not enforce an
application schema, canonical encoding, or unique map keys.

## Encode into caller buffers

```cpp
auto enc = make_resumable_encoder();
auto op = enc(number, text);
auto progress = op(first_buffer, buffer_batch, last_buffer);
for (const auto prefix : progress.produced()) {
    send_or_copy(prefix);
}
auto unused = progress.unused_output();
```

Writable chunks also support forward non-contiguous ranges. The buffers are
fixed writable ranges: the operation never grows them. Ordinary encoding writes
only during the feed call, returns `need_output` when capacity runs out, and
continues on the next buffer feed. `produced()` gives the written prefixes in
order; `unused_output()` gives the unused cursor suffix for reuse, including a
partially filled chunk. Retain the progress object while using these descriptor
views. The byte storage stays caller-owned and must survive any asynchronous
send that borrows it. `op.remaining_output()` exposes the latest unused suffix.

Encoder lvalues are borrowed until completion. Rvalues are retained by value,
but a retained view or reference wrapper still borrows its referent. Options and
codecs follow the decoder's factory pattern. `co_await op` returns the total
written byte count when encoding completes.

For an awaiting consumer, `enc.chunks(values...)` yields written prefixes through
the same `next()`/`value()` protocol. Supply caller-owned buffers with
`output(buffer)` or `output(lease_output(buffer))`. `lease_output` is a move-only
descriptor expressing permission to keep writing across yields; it does not
own or copy the bytes. Supplying a plain buffer grants the same permission.
The encoder retains unused capacity across yields. Consume a yielded prefix
before acknowledging it, and do not recycle a buffer while unread prefixes or
unused capacity still give the encoder access to it. No next buffer is allocated
automatically. The final partial prefix is also yielded and must be acknowledged.
For this generator, `remaining_output()` exposes its queued writable suffix.
After a failure to publish a prefix, that suffix also retains the partially
written descriptor; inspect its `produced()` prefix if recovery is needed.

## Customizations and failure

Ordinary forwarding customizations can keep returning `auto`:

```cpp
template <typename Context> auto encode(Context &enc) const {
    return enc(wrap_as_array{number, text});
}
template <typename Context> auto decode(Context &dec) {
    return dec(wrap_as_array{number, text});
}
```

The returned task retains temporary composition wrappers. A customization that
performs work across suspension uses the resumable context's `task_type` and
propagates every status, for example in the value type's namespace:

```cpp
template <typename Context>
typename Context::task_type resumable_decode(Context &dec, record &value) {
    const auto status = co_await dec(wrap_as_array{value.number, value.text});
    if (status != status_code::success) co_return status;
    co_return status_code::success;
}
```

The corresponding hook names are `resumable_encode` and `resumable_transcode`.
Codec mixins may provide hooks returning `resumable_task<status_code>`, including
decoder dispatch with an already-consumed header, or deduce `auto` for forwarding.
Use the concrete task type in a mixin return declaration because its CRTP `Self`
is still incomplete there. A synchronous codec requiring immediate bytes needs an
explicit resumable implementation; there is no hidden whole-item buffering or
replay fallback.

Hook tasks suspend through the active resumable context. Await transport or
framework operations in the application coroutine, then feed their byte buffers
to the operation; the inline driver does not register arbitrary external awaiters
inside codec tasks.

Failure is terminal and does not roll back bytes, destinations or output. A
decoded/written prefix may remain. Allocation failures map to `out_of_memory`.
Batch acceptance is also incremental: `accepted_chunks` reports the descriptor
prefix accepted before failure, and `remaining_input()` preserves its unread
portion. Keep that backing alive when inspecting or transferring it. Do not
treat a failed feed as atomic or blindly resubmit the original batch.

See [the standalone C++20 example](../examples/40_resumable.cpp) for mixed buffer
types, variadic values, runtime batches, and a raw coroutine waiting for input.
