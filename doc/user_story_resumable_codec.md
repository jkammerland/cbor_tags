# User Story: Add Explicit Resumable CBOR Codec Entry Points

## Story

As a maintainer of `cbor_tags`,
I want resumable decoding and encoding to use explicit APIs separate from the primary one-shot codec APIs,
so that retry, checkpoint, and coroutine behavior can be designed without making normal decode/encode carry partial state guarantees.

## Current Policy

The primary `make_decoder(...)(...)` and `make_encoder(...)(...)` paths are one-shot APIs. They may report `status_code::incomplete`, but callers must not treat that as permission to append data and retry the same decoder instance. Incomplete/error paths may have advanced internal cursor state or partially observed data.

Resumable behavior must be introduced through separate entry points, for example:

```cpp
auto session = make_resumable_decoder(source);

auto result = session.decode(value);
if (!result && result.error() == status_code::incomplete) {
    source.append_more(bytes);
    result = session.decode(value);
}
```

Encoding needs the same separation:

```cpp
auto session = make_resumable_encoder(sink);

auto result = session.encode(value);
if (!result && result.error() == status_code::incomplete) {
    sink.make_space();
    result = session.encode(value);
}
```

## Required Design Points

- Resumable state must store durable offsets or explicit parser state, not invalidatable non-contiguous iterators.
- Definite and indefinite CBOR forms must both be modeled; indefinite byte strings, text strings, arrays, and maps can all occur in streamed data.
- Container mutation must be transactional or explicitly staged until the decoded item is complete.
- Header wrappers such as `as_array`, `wrap_as_array`, and `as_map` must participate in the same checkpoint/state model as value decoding.
- Encoder resume must track output progress explicitly instead of relying on rollback of caller-owned buffers.

## Useful Implementation Sketch

Iterator state for non-contiguous buffers should be reconstructed from an offset at resume boundaries:

```cpp
struct resumable_cursor {
    std::size_t offset{};
};

template <class Buffer>
auto iterator_at(const Buffer& buffer, std::size_t offset) {
    return std::next(buffer.cbegin(), static_cast<std::ptrdiff_t>(offset));
}
```

The parser should own enough state to resume inside nested definite and indefinite items:

```cpp
enum class frame_kind {
    definite_array,
    definite_map,
    indefinite_array,
    indefinite_map,
    indefinite_bstr,
    indefinite_tstr,
};

struct parse_frame {
    frame_kind kind{};
    std::uint64_t remaining{};
};
```

## Acceptance Criteria

- Primary one-shot decode/encode docs do not promise retry-after-incomplete behavior.
- New resumable APIs are explicit and cannot be confused with `make_decoder` / `make_encoder`.
- Resumable decode handles complete and incomplete definite and indefinite payloads.
- Resumable encode handles bounded sinks without corrupting partially written caller buffers.
- Regression tests cover non-contiguous buffer growth by proving offset-based resume state survives iterator invalidation.
