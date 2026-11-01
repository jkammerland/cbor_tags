# Resumable encoding and decoding

Include `<cbor_tags/cbor_resumable.h>`, bind caller-owned storage once, and change
its availability before calling argument-free `resume()`. Operation creation
runs eagerly; `resume()` reports `need_input`, `need_output`, or terminal `done`.
Use `result()` to distinguish success from a terminal error.

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

Keep the chunk descriptors and their owners alive, and append without removing
or changing the retained prefix. The default adapter does not support
`pop_front()` reclamation; use an explicit custom source for that, as in the
[ring example](../examples/46_resumable_ring.cpp).

A flat `vector<byte>` or `deque<byte>` can instead receive appended bytes
directly. Typed decoding finishes once the requested values are complete;
`finish()` closes input and makes a truncated item return `status_code::incomplete`.
See the [record example](../examples/40_resumable.cpp).

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
encoded length. These default chunk descriptors stay append-only even after
publication; the [ring example](../examples/46_resumable_ring.cpp) shows explicit
publication and reclamation ownership.

Growable encoding appends after existing contents. See the
[bounded encoding example](../examples/41_resumable_encoding.cpp) for a complete
program that exposes writable chunks in two steps.

## Keep owners alive

Declare storage/source, factory, values, operation, and waiter in that order so
reverse destruction releases borrowers first. A storage-bound factory owns its
adapter, is immovable, and starts operations only as an lvalue; destroy each
operation before starting another on the same source, even after completion.

Successive operations retain the committed stream position and buffered tail.
Use `make_input_source(storage)` or `make_output_source(storage)` when the source
must outlive a factory or share its position between operation kinds; factories
borrow these explicit sources, and validators and generators require one.

Default adapters support flat and nested byte ranges, including owning inner
containers and borrowed spans. Between resumptions, append descriptors or grow
the last inner range while preserving earlier contents and order:

- Indexable storage may reallocate; positions are logical indexes. Node-based
  storage must preserve element iterators, including inner iterators when an
  outer container relocates owning ranges.
- Final-range growth needs a sized range or a changed end boundary. Unsized
  ranges must preserve their previously observed end iterator; an unchanged end
  sentinel cannot expose growth without a custom source.
- Shrinking, replacing earlier ranges, removing descriptors, or appending after
  closure violates the adapter's preconditions. Debug assertions check locally
  detectable violations; malformed CBOR and an already-acquired source remain
  checked errors.

Mutation and codec driving are serialized. Repeated `resume()` without new
availability is safe, but the producer should wait for a storage change rather
than busy-loop; empty input alone never signals EOF.

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
    } // Destroy the operation before acquiring this source again.

    input.push_back(std::span<const std::byte>{wire}.last(1));
    auto tail = decoder(second);
    assert(tail.done());
    assert(tail.result());
    assert(second == 1000);
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
The source stays acquired until operation destruction, and repeated `cancel()`
or `resume()` calls stay terminal.

Declare the waiter last: destroying it detaches its continuation, while a live
waiter must not outlive its pending operation. Moving an operation preserves
its waiter; a resumed waiter may destroy the completed operation, but destructive
reentrancy from a running codec is unsupported.

## Read a sequence or validate input

`make_resumable_generator<T>(source)` retains a current value and operation.
Call `next()` before driving, copy `value()` before requesting the next item,
and keep one outstanding request; clean EOF succeeds with zero bytes and sets
`done()`. See the [generator example](../examples/44_resumable_generator.cpp).

`make_resumable_validator(source)` checks one item without constructing values;
a numeric second argument selects a fixed item count, and `until_end_of_input`
waits for explicit closure. The third argument accepts `validation_options`,
whose default depth limit is 64; see the
[validation example](../examples/42_resumable_validation.cpp).

Validation checks CBOR structure and UTF-8, not an application schema.
Indefinite text chunks must each contain complete UTF-8 sequences; a declared
payload that is truncated at EOF remains `incomplete`.

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
ct::resumable<ct::status_code> resumable_decode(Decoder &decoder, message &value) {
    co_return co_await decoder(ct::wrap_as_array{value.name, value.values});
}

template <class Encoder>
ct::resumable<ct::status_code> resumable_encode(Encoder &encoder, const message &value) {
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

ADL, member, and codec-mixin hooks return exactly `resumable<status_code>`;
`resumable_transcode` is the symmetric alternative. Explicit hooks take
precedence over ordinary codecs; see the
[custom hook example](../examples/45_resumable_custom_hook.cpp).

Ordinary codecs retry an incomplete item by restoring its cursor, without
restoring destinations, codec members, callbacks, or external effects. Publish
changes only after the ordinary item succeeds, or retain progress in a hook.
Variant alternatives can be retried only before committing bytes.

Install mixins with `make_resumable_encoder<Codec...>` or
`make_resumable_decoder<Codec...>`, and select options with
`make_resumable_*_with_options<Options, Codec...>`. Member/ADL hooks follow the
automatic metadata-tag convention; mixin hooks own their complete wire shape.

Structural decoding commits completed headers, fields, and elements.
Reservable destinations may allocate from validated lengths before payload
arrival; use `as_bounded_size<0, limit>(values)` or a bounded allocator to impose
application limits, with allocation failure reported as `out_of_memory`.

With `codec::typed_array`, owning RFC 8746 arrays resume into numeric storage,
and encoding accepts owning arrays or borrowed ranges. Headers and complete
elements commit independently; the definite byte-string payload may cross
transport chunks, while borrowed views retain ordinary input-lifetime rules.

## Implement a reclaiming source

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
replace the default adapter with the complete
[ring source](../examples/46_resumable_ring.cpp), which also implements
`ResumableOutputSource` and demonstrates publication before reclamation.

`<cbor_tags/cbor_resumable_source.h>` defines both source concepts. Implement
their public members directly; declaring `position_type` selects the custom-source
contract:

- Positions are copyable and expose a monotonic `offset()` of exactly
  `std::size_t`; positions at and after commitment survive permitted mutations.
  Byte counts, sized-range sizes, and any `size_type` also use `std::size_t`.
- Input supplies logical byte traversal, `commit(position)`, and
  `remaining(position)`. Output supplies `value_type = std::byte`, `position()`,
  `commit()`, `restore(position)`, `remaining_output()`, and `write(bytes)`;
  a short write means temporary exhaustion.
- Both expose nonthrowing `committed_position()` and `release()`; output
  `restore(position)` is also nonthrowing. `acquire()` enforces one operation,
  `refresh()` runs before driving, and `eof()`/`close()` mark terminal availability.
- A sized input source reports its retained size and supplies
  `remaining_size(position)` for exact availability. A nonzero absolute offset
  is not an index into that retained size; unsized input needs no total extent.

Custom sources expose noncontiguous logical traversal even over contiguous
allocations; optional borrow/consume windows provide contiguous bulk access.
Temporary codec windows end before suspension. Decoded `span` and `string_view`
values instead retain application borrows, require stable backing storage, and
cannot represent bytes spanning separate allocations.

Reclaim bytes strictly before commitment only after every external borrow or
pending I/O using them has ended. Output publication tracks newly committed
ranges independently of reclamation. Bounded storage must fit an ordinary
codec's whole retry unit; use a resumable hook for larger values or reject them
under the application's capacity policy.
