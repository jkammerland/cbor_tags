# Resumable encoding and decoding

Include `<cbor_tags/cbor_resumable.h>`, bind caller-owned storage once, and change
its availability before calling argument-free `resume()`. Operation creation
runs eagerly; `resume()` reports `need_input`, `need_output`, or terminal `done`.
Once an operation is `done`, use `result()` to distinguish success from a
terminal error. Calling `result()` earlier throws `std::logic_error`.

## Custom codecs and supported combinations

Ordinary member, ADL, and mixin customizations need a resumable hook or an
explicit [retry declaration](#ordinary-retry-declarations). A factory does not
infer retry safety from the destination type or from a codec being supplied by
the library.

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
    ct::resumable<ct::status_code> resumable_decode(Decoder &decoder) {
        co_return co_await decoder(ct::wrap_as_array{first, second});
    }
};
```

When decoding `["a", "b"]`, a suspension after `first` completes retains that
field's progress; the completed result is
`first == "a"` and `second == "b"` for every input split. Repeating the ordinary
`decode` body after that suspension could append `"a"` twice. The hook avoids
repeating completed work without copying or replacing the destination.

| Combination | Contract |
| --- | --- |
| Core values, containers, and reflected records | Structural progress resumes at completed headers, fields, and elements. |
| Member, ADL, or mixin resumable hooks | The hook retains progress and owns its effects. Hooks take precedence over ordinary customizations. |
| Ordinary customizations | Require a declaration for each applicable customization and direction. |
| `codec::typed_array` | Owning array decoding and owning/borrowed-range encoding have native hooks. Other wrapper forms require a hook or retry declaration. |
| `codec::shared_ptr` | Rejected by resumable factories, including encoding. Graph scope updates require a state-preserving resumable implementation. Ordinary factories support this codec. |

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

Keep the chunk descriptors and their owners alive, and append without removing
or changing the retained prefix. The default adapter does not support
`pop_front()` reclamation; use an explicit custom source for that, as in the
[ring example](../examples/46_resumable_ring.cpp).

A flat `vector<byte>` or `deque<byte>` can instead receive appended bytes
directly. Typed decoding finishes once the requested values are complete;
`finish()` closes input and makes a truncated item return `status_code::incomplete`.
Closing applies to the source, even when the operation is already done: later
operations on it see the end of input.
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

Declare the waiter last: destroying it detaches its continuation. Destroying a
pending operation cancels it, so a waiter that outlives it wakes with
`status_code::cancelled`. Moving an operation preserves its waiter; a resumed
waiter may destroy the completed operation, but destructive reentrancy from a
running codec is unsupported.

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

Ordinary retries require an explicit declaration, described below. Variant
alternatives can be retried only before committing bytes.

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
transport chunks. Other wrappers need an applicable hook or retry declaration;
borrowed views retain ordinary input-lifetime rules.

### Ordinary retry declarations

`resumable_retry_traits<T, Codec = void>` defaults both `encode` and `decode`
to `false`. Specialize it before instantiating an operation. Omit `Codec` for
member or ADL customizations; for a mixin, specialize on its actual ordinary
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

A declaration covers the **whole ordinary call**, including nested codecs,
allocators, callbacks, codec members, and external effects. An incomplete call
restores its cursor but does not restore any of that state. Publish changes
only on success, or ensure repeating them is harmless under the application's
contract. The library does not prove the declaration. A declaration for a value
does not authorize an installed mixin override; each applicable customization
must declare its direction, including header/tag dispatch inside optionals and
variants. Tag-observer mixins also need a declaration for ordinary retry units:
a complete tag can be observed before the rest of a tagged header suspends.
Calling `context.ordinary(value)` from a hook uses the same declaration checks.

The entire ordinary retry unit must fit retained storage. Prefer hooks for
large values and for stateful codecs. A retry declaration cannot enable
`codec::shared_ptr` in a resumable factory.

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
Temporary codec windows end before suspension. [Decoded views](#borrow-decoded-strings-and-items)
instead hold source positions and keep reading through the source after their
operation ends.

Reclaim bytes strictly before commitment only after every decoded view,
external borrow, or pending I/O using them has ended. Output publication tracks newly committed
ranges independently of reclamation. Bounded storage must fit an ordinary
codec's whole retry unit; use a resumable hook for larger values or reject them
under the application's capacity policy.
