# Streaming / coroutine decode plan

This document sketches an incremental (streaming) decode API for `cbor_tags` that can suspend on incomplete input and resume later when more bytes have been appended to a buffer.

## Motivation / problem statement

Today the decoder mostly assumes the full CBOR item is present. `status_code::incomplete` exists, but many “short read” paths throw and get mapped to `status_code::error`. Also, multi-argument decoding is driven by a fold expression, which is not resumable without extra state (it restarts from the beginning).

The goal is to support:

- **Append-only buffers** (input grows over time).
- **Resumption** from the exact point where decoding stopped (no double-consumption, no re-decoding already-parsed items).
- **Optional progressive output** for some types (e.g. “show partial text as it arrives”).

## High-level design

### Key idea: “need more bytes” is a suspension point, not a return value

Instead of returning `status_code::incomplete` from deep inside decode and forcing the caller to re-enter from the top, the streaming decoder:

- checks if the next operation needs `N` bytes,
- **suspends** if the bytes are not available,
- and is later **resumed** by someone else once the buffer is longer.

In other words: the outer control loop is “append bytes → resume handle”, not “call decode again from scratch”.

### Resumption driven externally (what you described)

Yes, this is feasible: the decoder owns (or returns) a coroutine handle; `feed(...)` extends the buffer, and the user calls `resume()` to continue decoding.

Pseudo usage:

```cpp
std::vector<std::byte> buf;
cbor::tags::stream_decoder dec{buf};

MyType out{};
auto op = dec.begin(out); // creates an internal coroutine and returns a small handle

while (op.state() == cbor::tags::stream_state::need_more_data) {
  co_await socket_read_append(buf); // external I/O grows buf
  op.resume();                      // continue from previous suspension point
}

if (op.state() == cbor::tags::stream_state::done) {
  // out is decoded
} else {
  // op.error() has status_code
}
```

## Buffer / lifetime constraints

Streaming only stays correct if the decoder’s saved state remains valid across `feed()`:

- **Do not store raw pointers/iterators across suspension.** Store offsets and recompute pointers/iterators on resume.
- For non-contiguous containers, “append” may invalidate iterators; for a first streaming MVP it is reasonable to **restrict streaming to contiguous buffers** (e.g. `std::vector<std::byte>` / `std::span<const std::byte>` windows) or to use an internal staging buffer.
- Decoding into view types (`std::string_view`, `std::span<const std::byte>`, `tstr_view`, `bstr_view`) is tricky in a streaming context unless the backing storage is stable. For MVP: treat view decodes as **atomic** (only publish the view on completion).

## Proposed API surface (MVP)

### Types

```cpp
namespace cbor::tags {
  enum class stream_state : std::uint8_t { running, need_more_data, done, error };

  struct need_info {
    std::size_t min_additional_bytes; // minimal additional bytes to make progress
    // Optional: an enum describing where we are stuck (header/payload/element/etc).
  };

  class stream_decoder {
  public:
    explicit stream_decoder(/* buffer ref or buffer facade */);

    // Extend the buffer / make new bytes visible.
    void feed(std::span<const std::byte> chunk);

    template <typename T>
    auto begin(T& out) -> /* decode_handle<T> (non-owning) */;
  };
}
```

### Decode handle

The handle is small and non-owning; it proxies the coroutine handle owned by `stream_decoder`.

```cpp
struct decode_handle {
  stream_state state() const noexcept;
  status_code  error() const noexcept;      // valid iff state()==error
  need_info    need() const noexcept;       // valid iff state()==need_more_data
  void         resume();                    // continues until done/need_more_data/error
};
```

Notes:

- `resume()` should run until it either completes, hits an error, or reaches the next “need more bytes” await point.
- The library should not spin: if there is no new data, `resume()` should quickly return `need_more_data` again.

## How to suspend: `need_bytes(n)` awaitable

Internally, implement a tiny awaitable:

- `await_ready()` returns true if `available_bytes >= n`
- otherwise `await_suspend(h)` stores `h` in the decoder, records `n` in `need_info`, and returns control to the caller
- `await_resume()` is no-op

All read helpers become “try-read” style:

- `co_await need_bytes(1);` then read initial byte
- `co_await need_bytes(4);` then read `uint32`
- etc.

This is what makes primitive types resumable too (they are the easiest case).

## Multi-argument decoding without fold-expression restart

The current `operator()(args...)` uses a fold expression, which cannot “remember where it stopped”.

For streaming there are three viable approaches:

1) **Require the user to sequence args** (simple MVP):
   - user calls `begin(a)` then `begin(b)` etc. after each completes.

2) **Tuple + index state machine**:
   - store `std::tuple<Args&...>` and a current index `i`
   - decode arg `i`, then `++i`
   - on `need_more_data`, suspend with `i` preserved

3) **Non-templated “plan/VM”** (best long-term):
   - `begin<T>()` builds a compact program for T (cached per type)
   - runtime engine executes frames; suspension happens in the engine, not in per-T code
   - avoids “entrypoint explosion” and reduces compile-time bloat

For a first iteration, (1) or (2) is enough to prove the model.

## Semantics of “incomplete”: atomic vs progressive output

You asked: “should the user be allowed to read a partially decoded tstr?” — this is mainly a *policy* decision.

### Recommendation: default to atomic, offer opt-in progressive modes

**Atomic (default):**

- If decoding suspends with `need_more_data`, the *observable output* `T` is unchanged.
- This matches the existing “return status_code” mental model and avoids exposing half-valid aggregates.
- Implementation: decode into scratch state inside `stream_decoder` and commit at completion.

**Progressive (opt-in):**

- For *owning* outputs like `std::string`/`std::vector<std::byte>`, the decoder may append as bytes arrive.
- Users can “dare” to read partial output between resumes (e.g. display incremental text).
- This is safe-ish for memory as long as we avoid handing out raw views into a buffer that might reallocate. The partial data lives in the user’s container.

### Special case: UTF-8 boundaries for tstr

Progressively appending raw bytes of a text string can split a multi-byte UTF-8 sequence.

Offer two progressive text policies:

1) `partial_bytes`:
   - append whatever bytes are available
   - user might see invalid UTF-8 until more bytes arrive

2) `partial_utf8`:
   - append only complete UTF-8 sequences
   - keep an internal “utf8_tail” buffer (up to 3 bytes) between resumes

If/when UTF-8 validation is implemented, `partial_utf8` can be the “safe display” mode.

## Error model

Keep the existing `status_code` for semantic errors, but in the streaming API:

- “need more data” is represented by `stream_state::need_more_data` (+ `need_info`)
- “done” is `stream_state::done`
- failures are `stream_state::error` with a `status_code`

Internally, replace throw-on-short-read helpers with suspension points.

## Testing plan (incremental)

Add doctest cases that split inputs across many small chunks and assert:

- decoder suspends at the right boundary and resumes without consuming twice
- primitives resume (e.g. `uint64` split across chunks)
- arrays/maps resume across element boundaries
- progressive `std::string` mode appends as chunks arrive
- view types remain atomic (no partial views published)

## Stretch goals / future work

- Support CBOR **indefinite-length** strings/arrays (better for truly streaming producers).
- A streaming **encoder** with backpressure (“need more output space”) symmetry.
- A shared token/scan engine for streaming decode + visualization tooling.

