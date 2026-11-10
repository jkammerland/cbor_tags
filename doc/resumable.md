# Resumable encoding and decoding

The one-shot encoder and decoder (`make_encoder`, `make_decoder`) handle a whole
item per call, and running out of input or output space ends the call. A
resumable operation suspends instead and continues where it stopped.

Include `<cbor_tags/cbor_resumable.h>`, bind caller-owned storage once, then add
more input or output space and call argument-free `resume()`. Operation creation
runs eagerly; `resume()` reports `need_input`, `need_output`, or `done`, and
`done()` checks for the last without resuming. A done operation has a result:
`result()` tells success from a terminal error. Calling `result()` earlier
throws `std::logic_error`.

## Custom codecs and supported combinations

One-shot member, ADL, and mixin customizations need a resumable hook or an
explicit [retry declaration](#one-shot-retry-declarations). Retry safety is
never inferred from the destination type or from a codec being supplied by the
library. Library codecs provide their own hooks and declarations; the table
below lists them.

For a record containing two strings, retain progress in a hook:

```cpp
#include <cbor_tags/cbor_resumable.h>
#include <string>

namespace ct = cbor::tags;

struct record {
    std::string first;
    std::string second;

    template <class Decoder>
    auto decode(Decoder &decoder) {
        return decoder(ct::wrap_as_array{first, second});
    }

    template <class Decoder>
    ct::resumable_task<ct::status_code> resumable_decode(Decoder &decoder) {
        co_return co_await decoder(ct::wrap_as_array{first, second});
    }
};
```

When decoding `["a", "b"]`, a suspension after `first` completes retains that
field's progress; the completed result is
`first == "a"` and `second == "b"` for every input split. Repeating the one-shot
`decode` body after that suspension could append `"a"` twice. The hook avoids
repeating completed work without copying or replacing the destination.

| Combination | Contract |
| --- | --- |
| Core values, containers, and reflected records, including `as_named_map` | Structural progress resumes at completed headers, fields, map entries, and elements. |
| Member, ADL, or mixin resumable hooks | The hook retains progress and owns its effects. Hooks take precedence over one-shot customizations. |
| One-shot customizations | Require a declaration for each applicable customization and direction. |
| `codec::typed_array` | Owning arrays and `as_typed_array(...)` ranges have native hooks in both directions. Typed-array views have a decode retry declaration. Both also work inside `as_bounded_size`. Other wrapper forms require a hook or retry declaration. |
| `codec::shared_ptr` | Rejected by resumable encoders and decoders. Graph scope updates require a state-preserving resumable implementation. One-shot encoders and decoders support it. |
| [Sinks](#stream-decoded-data-to-callbacks) | String pieces reach the callback as they arrive and are committed once accepted. Elements and entries resume like container children. |

Changing transport chunk boundaries must preserve the values of every
supported combination. Include every input/output split in custom-codec tests;
for graph codecs, also check pointer identity.

## Decode incoming chunks

```cpp
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cassert>
#include <cstddef>
#include <deque>
#include <span>
#include <string>
#include <vector>

namespace ct = cbor::tags;

int main() {
    // This vector owns the bytes borrowed by both incoming chunks.
    std::vector<std::byte> wire;
    const std::string sent = "ready";
    const auto encoded = ct::make_encoder(wire)(sent);
    assert(encoded);

    std::deque<std::span<const std::byte>> input;
    auto decoder = ct::make_resumable_decoder(input);
    std::string received;
    auto operation = decoder(received);

    input.push_back(std::span<const std::byte>{wire}.first(2));
    const auto pending = operation.resume();
    assert(pending == ct::resume_state::need_input);

    input.push_back(std::span<const std::byte>{wire}.subspan(2));
    const auto completed = operation.resume();
    assert(completed == ct::resume_state::done);
    const auto result = operation.result();
    assert(result);
    assert(*result == wire.size());
    assert(received == sent);
}
```

Keep the chunks and their owners alive, and append without removing
or changing the retained prefix. The default adapter does not support
`pop_front()` reclamation; use an explicit custom source for that, as in the
[ring example](../examples/46_resumable_ring.cpp).

A flat `vector<byte>` or `deque<byte>` can instead receive appended bytes
directly. Typed decoding finishes once the requested values are complete; until
then, running out of input means waiting for more. If the rest of an item will
never come, [cancel](#await-completion-or-cancel) the operation. See the
[record example](../examples/40_resumable.cpp).

## Encode to a growable container

A `vector<byte>` or `deque<byte>` grows through ordinary append. No writable
chunks are needed, and encoding completes immediately when storage can grow.

```cpp
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cassert>
#include <cstddef>
#include <deque>
#include <string>

namespace ct = cbor::tags;

int main() {
    std::deque<std::byte> output;
    auto encoder = ct::make_resumable_encoder(output);
    const std::string sent = "ready";
    auto operation = encoder(sent);
    assert(operation.done());
    assert(operation.result());

    auto decoder = ct::make_decoder(output);
    std::string received;
    const auto decoded = decoder(received);
    assert(decoded);
    assert(received == sent);
}
```

## Expose bounded output capacity

Fixed storage and writable chunks suspend when capacity runs out. The caller
exposes more constructed bytes in the bound container, then resumes encoding:

```cpp
#include <cbor_tags/cbor_decoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <array>
#include <cassert>
#include <cstddef>
#include <deque>
#include <span>
#include <string>

namespace ct = cbor::tags;

int main() {
    // One owner supplies both writable chunks; unused capacity is never read.
    std::array<std::byte, 64> storage;
    std::deque<std::span<std::byte>> output;
    auto encoder = ct::make_resumable_encoder(output);
    const std::string sent = "ready for transport";
    auto operation = encoder(sent);

    output.push_back(std::span{storage}.first(4));
    const auto pending = operation.resume();
    assert(pending == ct::resume_state::need_output);

    output.push_back(std::span{storage}.subspan(4));
    const auto completed = operation.resume();
    assert(completed == ct::resume_state::done);
    const auto result = operation.result();
    assert(result);

    const auto committed =
        std::span<const std::byte>{storage}.first(operation.committed_bytes());
    auto decoder = ct::make_decoder(committed);
    std::string received;
    const auto decoded = decoder(received);
    assert(decoded);
    assert(received == sent);
}
```

Only `committed_bytes()` may be published; capacity and tentative writes are not
encoded length. If no more room will come, cancel the operation. These default
chunks stay append-only even after publication; the
[ring example](../examples/46_resumable_ring.cpp) shows explicit publication and
reclamation ownership.

Growable encoding appends after existing contents. See the
[bounded encoding example](../examples/41_resumable_encoding.cpp) for a complete
program that exposes writable chunks in two steps.

## Keep owners alive

Declare storage, any explicit source or output buffer, the resumable encoder or
decoder, values, operation, and waiter in that order so reverse destruction
releases borrowers first. An encoder or decoder bound directly to storage owns
its adapter, is immovable, and starts operations only as an lvalue; destroy each
operation before starting another on the same storage, even after completion.

Successive operations retain the committed stream position and buffered tail.
A failed or cancelled operation keeps what it committed, which can end inside an
item: the input position can point into a string payload or between array
elements, and the output can end with part of an item. Discard that input or
output instead of starting another operation on it.

Use `make_input_source(storage)` or `make_output_buffer(storage)` when the
source or output buffer must outlive one encoder or decoder, or share its
position between operation kinds. Encoders and decoders borrow them; validators
and generators require a source.

Default adapters support flat and nested byte ranges, including owning inner
containers and borrowed spans. Between resumptions, append chunks or grow
the last inner range while preserving earlier contents and order:

- Indexable storage may reallocate; positions are logical indexes. Node-based
  storage must preserve element iterators, including inner iterators when an
  outer container relocates owning ranges.
- Final-range growth needs a sized range or a changed end boundary. Unsized
  ranges must preserve their previously observed end iterator; an unchanged end
  sentinel cannot expose growth without a custom source or output buffer.
- Shrinking, replacing earlier ranges, or removing chunks violates the adapter's
  preconditions. Debug assertions check locally detectable violations; malformed
  CBOR and an already-acquired source or output buffer remain checked errors.

Mutation and codec driving are serialized. Repeated `resume()` without new
input or output space is safe, but the producer should wait for a storage change
rather than busy-loop. Running out of input or output space never ends an
operation by itself.

## Borrow decoded strings and items

A resumable decoder can return views into its input instead of copying. Name
the view types through the decoder, as with a one-shot decoder over
non-contiguous input ([borrowed decode views](experimental_ranges.md#borrowed-decode-views)):

```cpp
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <string>
#include <vector>

namespace ct = cbor::tags;

int main() {
    std::vector<std::byte> wire;
    const auto encoded = ct::make_encoder(wire)(std::string{"station"}, std::vector<int>{1, 2, 3});
    assert(encoded);

    std::vector<std::byte> input;
    auto decoder = ct::make_resumable_decoder(input);
    decltype(decoder)::tstr_view_t name;
    decltype(decoder)::encoded_array_view values;
    {
        auto operation = decoder(name, values);
        input.insert(input.end(), wire.begin(), wire.end());
        operation.resume();
        assert(operation.result());
    }

    input.reserve(input.capacity() * 2); // The views survive reallocation.
    assert(std::string(name) == "station");
    assert(std::ranges::distance(values.bytes()) == 4); // 83 01 02 03
}
```

The decoder provides `tstr_view_t`, `bstr_view_t`, `encoded_item_view`,
`encoded_array_view`, and `encoded_map_view`. They point into the source, not
the operation, so they outlive the operation, and with the default adapters
they survive reallocation of the bound storage. Keep the source or the
storage-bound decoder alive, and don't reclaim the bytes while a view uses them.

`std::string_view` and `std::span<const std::byte>` can't be decoded resumably.
They need contiguous input and resumable sources are never contiguous, so the
operation fails with `status_code::contiguous_view_on_non_contiguous_data` as
soon as the string header arrives.

Each view completes in one step: its whole string or item must be in retained
storage at once. Bounded storage, such as a ring, must be large enough for it.
A [sink](#stream-decoded-data-to-callbacks) takes a string in pieces instead.

## Stream decoded data to callbacks

A sink decodes into a callback instead of a destination. String payloads reach
the callback in pieces as input arrives, and arrays and maps reach it one
element or entry at a time, so nothing has to hold the whole value:

| Function | Callback arguments |
| --- | --- |
| `as_bstr_sink(f)` | `std::span<const std::byte>`, one call per piece |
| `as_tstr_sink(f)` | `std::string_view`, one call per piece |
| `as_array_sink<T>(f)` | `T&&`, one call per element |
| `as_map_sink<K, V>(f)` | `K&&, V&&`, one call per entry |

```cpp
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

namespace ct = cbor::tags;

int main() {
    std::vector<std::byte> wire;
    const std::vector<std::byte> image(5'000, std::byte{0x2a});
    const std::vector<std::uint32_t> readings{3, 5, 8};
    const auto encoded = ct::make_encoder(wire)(image, readings);
    assert(encoded);

    std::deque<std::span<const std::byte>> input;
    auto decoder = ct::make_resumable_decoder(input);
    std::size_t image_size{};
    std::uint64_t total{};
    auto operation = decoder(
        ct::as_bstr_sink([&](std::span<const std::byte> piece) {
            image_size += piece.size();
            return image_size <= 1'000'000 ? ct::status_code::success : ct::status_code::size_limit_exceeded;
        }),
        ct::as_array_sink<std::uint32_t>([&](std::uint32_t reading) { total += reading; }));

    // Each chunk reaches the image sink before the next one arrives
    for (std::size_t offset = 0; offset < wire.size(); offset += 1'000) {
        input.push_back(std::span<const std::byte>{wire}.subspan(offset, std::min<std::size_t>(1'000, wire.size() - offset)));
        operation.resume();
    }
    assert(operation.result());
    assert(image_size == image.size());
    assert(total == 16);
}
```

The callback returns `void` or `status_code`. Any status other than
`status_code::success` stops the decode with that status, and an exception
stops it with `status_code::error`. Sinks take no `as_bounded_size` limits;
check sizes in the callback as above.

A piece is only valid during the call. Where pieces split depends on the
input's storage, so they needn't match the chunks of an indefinite-length
string, and a text piece can end inside a UTF-8 sequence. Empty strings and
chunks make no call.

A piece points into the source when the source lends
[windows](#implement-a-reclaiming-source-or-output-buffer), otherwise into a
copy. Either way it's committed once the callback accepts it, so bounded storage
only has to fit the string's head. The
[ring example](../examples/46_resumable_ring.cpp) passes a byte string many
times larger than its ring this way.

The functions return `bstr_sink<F>`, `tstr_sink<F>`, `array_sink<T, F>`, and
`map_sink<K, V, F>`. A record field can be a sink too, so one field streams
while the others decode as usual. One-shot decoders accept sinks as well, and
contiguous input passes a definite byte string as a single piece.

## Await completion or cancel

```cpp
#include "resumable_consumer.h" // Example-only owner from examples/.
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cassert>
#include <cstddef>
#include <deque>
#include <span>
#include <vector>

namespace ct = cbor::tags;

template <class Operation>
example::consumer observe_cancellation(Operation &operation, std::size_t &notifications) {
    const auto result = co_await operation;
    ++notifications;
    co_return !result && result.error() == ct::status_code::cancelled;
}

int main() {
    std::vector<std::byte> wire;
    const auto encoded = ct::make_encoder(wire)(7, 1000);
    assert(encoded);

    std::deque<std::span<const std::byte>> input;
    auto decoder = ct::make_resumable_decoder(input);
    int first{}, second{};
    std::size_t notifications{};
    {
        auto operation = decoder(first, second);
        auto waiter = observe_cancellation(operation, notifications);

        input.push_back(std::span<const std::byte>{wire}.first(wire.size() - 1));
        const auto pending = operation.resume();
        assert(pending == ct::resume_state::need_input);
        assert(first == 7);
        const auto committed = operation.committed_bytes();

        operation.cancel();
        assert(waiter.succeeded()); // The example waiter observed cancellation.
        assert(notifications == 1);
        assert(operation.committed_bytes() == committed);
    } // This input stays discarded after the cancel.
}
```

`co_await operation` observes completion while a producer supplies storage and
calls `resume()`. Completion resumes the waiter inline; see the
[coroutine example](../examples/43_resumable_coroutine.cpp) and its
[example-only owner](../examples/resumable_consumer.h).

Cancellation destroys suspended frames and wakes the waiter with
`status_code::cancelled`, preserving committed fields or elements within a
larger item. It discards tentative output without undoing published I/O,
destination changes, or application effects; I/O cancellation is independent.
The source or output buffer stays acquired until operation destruction, and
repeated `cancel()` or `resume()` calls stay terminal. Discard the input or
output afterwards, as after a failure (see [keep owners alive](#keep-owners-alive)).

Declare the waiter last: destroying it detaches its continuation. Destroying a
pending operation cancels it, so a waiter that outlives it wakes with
`status_code::cancelled`. Moving an operation preserves its waiter; a resumed
waiter may destroy the completed operation, but destructive reentrancy from a
running codec is unsupported.

## Read a sequence or validate input

`make_resumable_generator<T>(source)` retains a current value and operation.
Call `next()` before driving, copy `value()` before requesting the next item,
and keep one outstanding request. See the
[generator example](../examples/44_resumable_generator.cpp).

`make_resumable_validator(source)` checks one item without constructing values;
a numeric second argument selects a fixed item count. The third argument accepts
`validation_options`, whose default depth limit is 64; see the
[validation example](../examples/42_resumable_validation.cpp).

Validation checks CBOR structure and UTF-8, not an application schema.
Indefinite text chunks must each contain complete UTF-8 sequences.

A sequence has no end marker, so the operation for the next item waits when the
input stops. When the transport ends, that waiting operation tells a clean end
from a cut-off item: if it has committed nothing and holds no received bytes, no
part of another item arrived. Then cancel it:

```cpp
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cassert>
#include <cstddef>
#include <ranges>
#include <vector>

namespace ct = cbor::tags;

int main() {
    std::vector<std::byte> wire;
    const auto encoded = ct::make_encoder(wire)(7, 1000);
    assert(encoded);

    // The transport delivers 7 and part of 1000, then ends.
    std::vector<std::byte> input(wire.begin(), wire.end() - 1);
    auto source = ct::make_input_source(input);
    auto items = ct::make_resumable_generator<int>(source);
    std::vector<int> received;
    for (;;) {
        auto &operation = items.next();
        if (!operation.done()) {
            const bool clean = operation.committed_bytes() == 0 &&
                               std::ranges::empty(operation.remaining_input());
            assert(!clean); // 1000 was cut off
            operation.cancel();
            break;
        }
        assert(operation.result());
        received.push_back(items.value());
    }
    assert(received == std::vector<int>{7});
}
```

One-item validators in a loop end a validated sequence the same way.

## Compose custom codecs

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
ct::resumable_task<ct::status_code> resumable_decode(Decoder &decoder, message &value) {
    co_return co_await decoder(ct::wrap_as_array{value.name, value.values});
}

template <class Encoder>
ct::resumable_task<ct::status_code> resumable_encode(Encoder &encoder, const message &value) {
    co_return co_await encoder(ct::wrap_as_array{value.name, value.values});
}

int main() {
    std::vector<std::byte> wire;
    auto encoder = ct::make_resumable_encoder(wire);
    const message sent{"station", {1, 24, 1000}};
    auto encoding = encoder(sent);
    assert(encoding.done());
    assert(encoding.result());

    std::vector<std::byte> input;
    auto decoder = ct::make_resumable_decoder(input);
    message received;
    auto decoding = decoder(received);
    input.insert(input.end(), wire.begin(), wire.end());
    const auto completed = decoding.resume();
    assert(completed == ct::resume_state::done);
    assert(decoding.result());
    assert(received.name == sent.name);
    assert(received.values == sent.values);
}
```

ADL, member, and codec-mixin hooks return exactly `resumable_task<status_code>`;
`resumable_transcode` is the symmetric alternative. Explicit hooks take
precedence over one-shot codecs; see the
[custom hook example](../examples/45_resumable_custom_hook.cpp).

One-shot retries require an explicit declaration, described below. Variant
alternatives can be retried only before committing bytes.

Install mixins with `make_resumable_encoder<Codec...>` or
`make_resumable_decoder<Codec...>`, and select options with
`make_resumable_*_with_options<Options, Codec...>`. Member/ADL hooks follow the
automatic metadata-tag convention; mixin hooks own their complete wire shape.

Structural decoding commits completed headers, fields, and elements.
Reservable destinations may allocate from validated lengths before payload
arrival; use `as_bounded_size<0, limit>(values)` or a bounded allocator to impose
application limits, with allocation failure reported as `out_of_memory`.

With `codec::typed_array`, owning RFC 8746 arrays and `as_typed_array(...)`
ranges resume in both directions, with or without `as_bounded_size`. Decoding
follows the one-shot
[destination rules](rfc8746_typed_arrays.md#copied-and-borrowed-decode-forms)
and checks bounds and the destination size before it commits the header.
Headers and complete elements commit independently; the definite byte-string
payload may cross transport chunks, as in the
[typed-array example](../examples/47_resumable_typed_array.cpp).

Typed-array views are [decoded views](#borrow-decoded-strings-and-items). Name
them through the decoder:

```cpp
auto decoder = ct::make_resumable_decoder<ct::codec::typed_array>(input);
ct::rfc8746::typed_array_view_for<float, decltype(decoder)> samples;
auto operation = decoder(samples);
```

Use `typed_array_view_be_for` for big-endian views. The library declares their
one-shot decoding [retry-safe](#one-shot-retry-declarations), with or without
`as_bounded_size`, so a view commits nothing until its whole item has arrived.
A `typed_array_view<T>` stores a span and fails like `std::span<const std::byte>`.
Other wrappers need an applicable hook or retry declaration.

### One-shot retry declarations

`resumable_retry_traits<T, Codec = void>` defaults both `encode` and `decode`
to `false`. Specialize it before instantiating an operation. Omit `Codec` for
member or ADL customizations; for a mixin, specialize on its actual one-shot
`Codec<Transcoder>` instantiation:

```cpp
// transactional_record's decoder must publish changes only after success.
template <>
struct cbor::tags::resumable_retry_traits<transactional_record> {
    static constexpr bool encode = false;
    static constexpr bool decode = true;
};

// Authorize only this value/codec pair; other mixins remain undeclared.
template <class Transcoder>
struct cbor::tags::resumable_retry_traits<my_value, my_codec<Transcoder>> {
    static constexpr bool encode = true;
    static constexpr bool decode = true;
};
```

A declaration covers the **whole one-shot call**, including nested codecs,
allocators, callbacks, codec members, and external effects. An incomplete call
restores its cursor but does not restore any of that state. Publish changes
only on success, or ensure repeating them is harmless under the application's
contract. The library does not prove the declaration. A declaration for a value
does not authorize an installed mixin override; each applicable customization
must declare its direction, including header/tag dispatch inside optionals and
variants. Tag-observer mixins also need a declaration for one-shot retry units:
a complete tag can be observed before the rest of a tagged header suspends.
Calling `context.one_shot(value)` from a hook uses the same declaration checks.

The entire one-shot retry unit must fit retained storage. Prefer hooks for
large values and for stateful codecs. A retry declaration cannot enable
`codec::shared_ptr` in a resumable encoder or decoder.

## Implement a reclaiming source or output buffer

```cpp
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cbor_tags/cbor_resumable_source.h>
#include <cassert>
#include <cstddef>
#include <vector>

namespace ct = cbor::tags;

int main() {
    std::vector<std::byte> wire;
    const auto encoded = ct::make_encoder(wire)(1000);
    assert(encoded);

    std::vector<std::byte> input;
    auto source = ct::make_input_source(input);
    static_assert(ct::ResumableInputSource<decltype(source)>);
    auto decoder = ct::make_resumable_decoder(source);
    int received{};
    auto operation = decoder(received);

    input.insert(input.end(), wire.begin(), wire.end());
    const auto completed = operation.resume();
    assert(completed == ct::resume_state::done);
    assert(operation.result());
    assert(received == 1000);
}
```

An explicit source binds the same way whether it comes from `make_input_source`
or an application implementation of `ResumableInputSource`. For bounded reuse,
replace the default adapter with a ring like the one in
[example 46](../examples/46_resumable_ring.cpp), which also implements
`ResumableOutputBuffer` and demonstrates publication before reclamation.

`<cbor_tags/cbor_resumable_source.h>` defines both concepts. Implement their
public members directly; declaring `position_type` selects this contract:

- Positions are copyable and expose a monotonic `offset()` of exactly
  `std::size_t`; positions at and after commitment survive permitted mutations.
  Byte counts, sized-range sizes, and any `size_type` also use `std::size_t`.
- Input supplies logical byte traversal, `commit(position)`, and
  `remaining(position)`. Output supplies `value_type = std::byte`, `position()`,
  `commit()`, `restore(position)`, `remaining_output()`, and `write(bytes)`;
  a short write means temporary exhaustion.
- Both expose nonthrowing `committed_position()` and `release()`; output
  `restore(position)` is also nonthrowing. `acquire()` enforces one operation,
  and `refresh()` runs before driving.
- A sized input source reports its retained size and supplies
  `remaining_size(position)` with the exact count of bytes left. A nonzero
  absolute offset is not an index into that retained size; unsized input needs
  no total extent.

Custom sources expose noncontiguous logical traversal even over contiguous
allocations. Sources and output buffers can also lend windows, contiguous
runs of their storage that the codec copies in bulk and
[sinks](#stream-decoded-data-to-callbacks) read in place:

- Input: `borrow_window(position) const` returns
  `std::optional<std::span<const std::byte>>` with the available bytes from
  `position` to the end of a run, and `consume_window(position, count) const`
  advances `position` past `count` of them.
- Output: `borrow_window()` returns `std::optional<std::span<std::byte>>` with
  writable storage at `position()`, and `consume_window(count)` marks `count`
  of those bytes written.

Return `std::nullopt` where there is no window. Windows end before the
codec suspends. [Decoded views](#borrow-decoded-strings-and-items) instead hold
source positions and keep reading through the source after their operation
ends.

Reclaim bytes strictly before commitment only after every decoded view,
external borrow, or pending I/O using them has ended. Output publication tracks
newly committed ranges independently of reclamation.

Bounded storage must fit the largest unit that is retried whole, in both
directions. For built-in types that's one CBOR head, at most 9 bytes: string
payloads pass through in pieces, and arrays and maps one element or entry at a
time. `codec::typed_array` arrays and ranges need up to 18 bytes for their tag
and byte-string heads together, and take each element whole. A
[decoded view](#borrow-decoded-strings-and-items), typed-array views included,
or a value with a [retry declaration](#one-shot-retry-declarations) needs its
whole item. Use a resumable hook for larger values or reject them under the
application's capacity policy.

A unit larger than the storage leaves the operation waiting for room that
reclaiming can't make. A source or output buffer can report that with
`bool at_capacity() const noexcept`, which the operation checks whenever the
codec suspends: return true when everything after the committed position, the
bytes to read or the room to write, already spans all of the storage. The
operation then fails with `status_code::capacity_exceeded` instead of waiting.
The [ring example](../examples/46_resumable_ring.cpp) implements it for both
directions.
