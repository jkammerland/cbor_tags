# Resumable encoding and decoding

The encoder and decoder (`make_encoder`, `make_decoder`) process the requested
items within one call. Running out of input or output space ends that call. Use a resumable
operation to wait for more storage and continue where it stopped.

Include `<cbor_tags/cbor_resumable.h>`, bind caller-owned storage once, then add
more input or output space and call argument-free `resume()`. Operation creation
runs eagerly; `resume()` reports `need_input`, `need_output`, or `done`, and
`done()` checks for the last without resuming. A done operation has a result:
`result()` tells success from a terminal error. Calling `result()` earlier
throws `std::logic_error`.

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
    auto encoder = ct::make_encoder(wire);
    const auto encode_result = encoder(sent);
    assert(encode_result);

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
    const auto decode_result = decoder(received);
    assert(decode_result);
    assert(received == sent);
}
```

Encoding appends after the contents the container had when the encoder or
output buffer was bound to it. Bytes the application adds after that count as
room, and the next write replaces them. Put bytes of your own, such as a frame
header, into the container before binding the encoder.

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
    const auto decode_result = decoder(received);
    assert(decode_result);
    assert(received == sent);
}
```

Only `committed_bytes()` may be published; capacity and tentative writes are not
encoded length. If no more room will come, cancel the operation. These default
chunks stay append-only even after publication; the
[ring example](../examples/46_resumable_ring.cpp) shows explicit publication and
reclamation ownership.

See the [bounded encoding example](../examples/41_resumable_encoding.cpp) for
a complete program that exposes writable chunks in two steps.

## Keep owners alive

Declare storage, any explicit source or output buffer, the resumable encoder or
decoder, values, operation, and waiter in that order so reverse destruction
releases borrowers first. An encoder or decoder bound directly to storage owns
its adapter, is immovable, and starts operations only as an lvalue; destroy each
operation before starting another on the same storage, even after completion.
`decltype(decoder)::operation` names the operation type, for example to keep the
current one in a `std::optional`; `reset()` it before starting the next.

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
containers and borrowed spans. Input may bind through a const reference to the
storage while a producer appends through a mutable reference. Output requires
writable bytes. Between resumptions, append chunks or grow the last inner range
while preserving earlier contents and order:

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
the view types through the decoder, as with an ordinary decoder over
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
    auto encoder = ct::make_encoder(wire);
    const auto encode_result = encoder(std::string{"station"}, std::vector<int>{1, 2, 3});
    assert(encode_result);

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
    auto encoder = ct::make_encoder(wire);
    const auto encode_result = encoder(image, readings);
    assert(encode_result);

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
while the others decode as usual. Ordinary decoders accept sinks as well, and
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
    auto encoder = ct::make_encoder(wire);
    const auto encode_result = encoder(7, 1000);
    assert(encode_result);

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

A sequence has no end marker. After the transport ends, requesting another
item still waits for input. Check that none of that item arrived, then cancel
the wait:

```cpp
#include <cbor_tags/cbor_encoder.h>
#include <cbor_tags/cbor_resumable.h>
#include <cassert>
#include <cstddef>
#include <ranges>
#include <vector>

namespace ct = cbor::tags;

int main() {
    std::vector<std::byte> input;
    auto encoder = ct::make_encoder(input);
    const auto encode_result = encoder(7, 1000);
    assert(encode_result);

    auto source = ct::make_input_source(input);
    auto items = ct::make_resumable_generator<int>(source);

    auto &first = items.next();
    assert(first.done());
    assert(first.result());
    assert(items.value() == 7);

    auto &second = items.next();
    assert(second.done());
    assert(second.result());
    assert(items.value() == 1000);

    // The transport has ended after two complete items.
    auto &next = items.next();
    assert(!next.done());
    const bool clean_end = next.committed_bytes() == 0 &&
                           std::ranges::empty(next.remaining_input());
    assert(clean_end);
    next.cancel();
}
```

If the last item was cut off, `clean_end` is false: the operation has either
committed a prefix or retained bytes that still need decoding. Report truncated
input and discard the unfinished item. One-item validators use the same check.
The [generator example](../examples/44_resumable_generator.cpp) shows a consumer
loop with a producer that delivers more input before ending the sequence.

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

Inside a hook, `co_await decoder(fields...)` or `co_await encoder(fields...)`
retains progress in those fields across input or output deliveries. A hook may
wait only through these calls or the [retry helper](#delegate-to-a-codec).
Perform unrelated asynchronous work between calls to `resume()`.

Member, ADL, and mixin `encode`/`decode` customizations need a resumable hook or
an explicit [retry declaration](#retry-declarations). Library codecs follow the
same rule:

| Combination | Contract |
| --- | --- |
| Core values, containers, and reflected records, including `as_named_map` | Structural progress resumes at completed headers, fields, map entries, and elements. |
| Member, ADL, or mixin resumable hooks | The hook retains progress and owns its effects. Hooks take precedence over ordinary customizations. |
| Ordinary customizations | Require a declaration for each applicable customization and direction. |
| `codec::typed_array` | Owning arrays and `as_typed_array(...)` ranges have native hooks in both directions. Typed-array views have a decode retry declaration. Both also work inside `as_bounded_size`. Other wrapper forms require a hook or retry declaration. |
| `codec::shared_ptr` | Rejected by resumable encoders and decoders. Graph scope updates require a state-preserving resumable implementation. Ordinary encoders and decoders support it. |
| [Sinks](#stream-decoded-data-to-callbacks) | String pieces reach the callback as they arrive and are committed once accepted. Elements and entries resume like container children. |

Structural decoding commits completed headers, fields, and elements.
Reservable destinations may allocate from validated lengths before payload
arrival; use `as_bounded_size<0, limit>(values)` or a bounded allocator to impose
application limits, with allocation failure reported as `out_of_memory`.

A value inside another value is handled by a call inside its parent's, so stack
use follows the nesting depth of the data, as it does for ordinary encoders and
decoders. Only a recursive type lets the input choose that depth; see
[Recursive Decode Paths](decoder_resource_limits.md#recursive-decode-paths).

Changing transport chunk boundaries must preserve decoded values. Test custom
codecs at every input/output split relevant to their wire representation.

ADL, member, and codec-mixin hooks return exactly `resumable<status_code>`;
`resumable_transcode` is the symmetric alternative. A hook takes its value as
an lvalue. An encode hook takes it as `const`, and a member encode hook is a
`const` member, so a const value reaches the hook too. Explicit hooks take
precedence over ordinary codecs; see the
[custom hook example](../examples/45_resumable_custom_hook.cpp).

Install mixins with `make_resumable_encoder<Codec...>` or
`make_resumable_decoder<Codec...>`, and select options with
`make_resumable_*_with_options<Options, Codec...>`. Member/ADL hooks follow the
automatic metadata-tag convention; mixin hooks own their complete wire shape.
A mixin's hook runs for values of the type it names. A value that only converts
to that type, or whose type derives from it, is not the mixin's, and installed
ordinary codecs work the same way.
A mixin's hook for a type goes before that type's own member or ADL hook, the
same order that `make_encoder` and `make_decoder` use for installed codecs. A
mixin with only `encode` or `decode` cannot override a type that has its own
resumable hook: the two modes would select different customizations. Give the mixin a `resumable_encode` or
`resumable_decode` for that type too.

Overloads from all installed mixins participate in normal C++ overload
resolution, independently of installation order. A matching `resumable_encode`
or `resumable_decode` takes precedence over `resumable_transcode`. Keep the
overload signatures and constraints consistent with the ordinary codecs so
both modes select the same behavior.

Derive a mixin that defines `encode` or `decode` from
`codec::encoder_base<Self>`, `codec::decoder_base<Self>`, or
`codec::base<Self>`, as [codec extensions](codec_extensions.md) describes. One
without the base doesn't compile when an ordinary encoder or decoder would give
it a value that the resumable side resumes structurally, such as a string,
container, optional, variant, tuple, or reflected record: the resumable side
would never call it.

The decoder's [entry points](codec_extensions.md#decoder-entry-points)
also determine which decode form runs when a resumable decoder retries a codec.

Each operation builds its own mixin objects, one for hooks and one for the
ordinary codec. A mixin's data members carry nothing from one to the other, or
from one operation to the next.

### Delegate to a codec

Use `retry(value)` when a resumable hook needs the ordinary codec for that same
type. Calling `decoder(value)` inside a hook for `value` would invoke the hook
again. `decoder.retry(value)` bypasses resumable hooks, tries the ordinary
`decode` overloads immediately, and waits and retries if the input is incomplete.

This hook decodes a double, then checks that it is finite. Input arrives one
byte at a time, so the same awaited call retries repeatedly before succeeding:

```cpp
#include "cbor_tags/cbor_encoder.h"
#include "cbor_tags/cbor_resumable.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

namespace ct = cbor::tags;

// Delegate double decoding, then apply an application rule.
template <typename Self> struct finite_doubles : ct::codec::decoder_base<Self> {
    using ct::codec::decoder_base<Self>::decode;

    ct::resumable<ct::status_code> resumable_decode(double &value) {
        auto &decoder = static_cast<Self &>(*this);
        const auto status = co_await decoder.retry(value);
        if (status != ct::status_code::success)
            co_return status;
        co_return std::isfinite(value) ? ct::status_code::success : ct::status_code::error;
    }
};

int main() {
    const double sent = 1.25;
    std::vector<std::byte> wire;
    auto encoder = ct::make_encoder(wire);
    const auto encode_result = encoder(sent);
    assert(encode_result);

    // The hook awaits one retry operation while input arrives byte by byte.
    std::vector<std::byte> input;
    auto decoder = ct::make_resumable_decoder<finite_doubles>(input);
    double received{};
    auto operation = decoder(received);
    assert(!operation.done());

    std::size_t next_byte{};
    for (;;) {
        input.push_back(wire.at(next_byte++));
        if (operation.resume() == ct::resume_state::done) {
            break;
        }
    }

    const auto decode_result = operation.result();
    assert(decode_result);
    assert(received == sent);
    return 0;
}
```

See the executable [retry example](../examples/48_resumable_retry.cpp).
The inherited `decode` overloads retain ordinary double decoding.
The validation hook runs only during resumable dispatch. It also stays bypassed
inside a larger value decoded through a [retry declaration](#retry-declarations).

`retry(value)` keeps that call's input consumption or output writes tentative
until it succeeds. A retry restores the cursor; it does **not** restore the
destination, codec members, allocations, callbacks, or external effects.
Customizations therefore require the [retry declaration](#retry-declarations).
Variant alternatives can be retried only before committing bytes.

On a decoder, `retry(value)` accepts scalar values, tags, views, and declared
customizations that can restart safely. Built-in strings, containers, optionals,
variants, tuples, and records must use `decoder(value)` to retain their progress.
On an encoder, `retry(value)` can also encode those composite values because its
output remains tentative until the whole call succeeds. The complete input or
output for a retry must fit retained storage.

A hook must wait only through `encoder(values...)`, `decoder(values...)`, or
`retry(value)`. Unrelated waits are not tracked by the operation: `resume()` would
still report `need_input` or `need_output`, and destroying the operation would
invalidate a continuation held elsewhere.

### Typed arrays

With `codec::typed_array`, owning RFC 8746 arrays and `as_typed_array(...)`
ranges resume in both directions, with or without `as_bounded_size`. Decoding
follows the ordinary
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
ordinary decoding [retry-safe](#retry-declarations), with or without
`as_bounded_size`, so a view commits nothing until its whole item has arrived.
A `typed_array_view<T>` stores a span and fails like `std::span<const std::byte>`.
Other wrappers need an applicable hook or retry declaration.

### Retry declarations

`resumable_retry_traits<T, Codec = void>` defaults both `encode` and `decode`
to `false`. Specialize it before instantiating an operation; a flag the
specialization leaves out stays `false`. Omit `Codec` for member or ADL
customizations; for a mixin, specialize on its `Codec<Transcoder>` instantiation for the
encoder or decoder that runs the retried call:

```cpp
// transactional_record's decoder must publish changes only after success.
template <>
struct cbor::tags::resumable_retry_traits<transactional_record> {
    static constexpr bool decode = true;
};

// Authorize only this value/codec pair; other mixins remain undeclared.
template <class Transcoder>
struct cbor::tags::resumable_retry_traits<my_value, my_codec<Transcoder>> {
    static constexpr bool encode = true;
    static constexpr bool decode = true;
};
```

A declaration covers the **complete encode or decode call**, including nested codecs,
allocators, callbacks, codec members, and external effects. An incomplete call
restores its cursor but does not restore any of that state. Publish changes
only on success, or ensure repeating them is harmless under the application's
contract. The library does not prove the declaration. A declaration for a value
does not authorize an installed mixin override; each applicable customization
must declare its direction, including header/tag dispatch inside optionals and
variants. Tag-observer mixins also need a declaration for calls being retried:
a complete tag can be observed before the rest of a tagged header suspends.
Calling `retry(value)` from a hook uses the same declaration checks.
Built-in encoding also checks customizations reached through enums, container
elements, variant alternatives, and record fields. A declared customization
owns its whole call; the library does not inspect its body or independently
authorize the calls it makes.

A retry runs the call again from the start, including the members it
already read. A `decode` that reads straight into a string or container member
adds to it again each time, so read into locals and assign them once the call
succeeds:

```cpp
struct transactional_record {
    int id{};
    std::string name;
    int count{};

    template <typename Decoder> auto decode(Decoder &decoder) {
        int new_id{};
        std::string new_name;
        int new_count{};

        auto result = decoder(new_id, new_name, new_count);
        if (result) {
            id = new_id;
            name = std::move(new_name);
            count = new_count;
        }
        return result;
    }
};
```

Prefer hooks for large values and for stateful codecs. A retry declaration cannot enable
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
    auto encoder = ct::make_encoder(wire);
    const auto encode_result = encoder(1000);
    assert(encode_result);

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
- Input supplies logical byte traversal with its const iterator as
  `position_type`, `commit(position)`, and `remaining(position)`. Output is a
  forward range over the bytes it holds and supplies `value_type = std::byte`,
  `size_type`, `position()`, `commit()`, `restore(position)`,
  `remaining_output()`, and `write(bytes)`; a short write means temporary
  exhaustion.
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
or a value with a [retry declaration](#retry-declarations) needs its
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
