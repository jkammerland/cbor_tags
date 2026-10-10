# Container interoperability

Containers are serialized **values** here. Value support does not make a
container a suitable input or output byte buffer. Buffer concepts and byte
ordering have separate requirements.

Use the ordinary encoder and decoder; there is no container codec to select:

```cpp
std::vector<unsigned char> wire{0x84, 3, 1, 1, 2, 7};
std::set<int> values{9};
auto dec = cbor::tags::make_decoder(wire);
auto result = dec(values);  // values == {1, 2, 3, 9}
int following{};
auto next = dec(following); // following == 7
```

Decoding appends/inserts into the destination. Unique sets collapse equivalent
elements; multisets retain them. Every declared wire element is consumed even
if insertion does not increase the destination size. Ordered sets use comparator
order, hash sets their iteration order, and hive/hub their own iteration rules.
Encoding does not sort them or promise a canonical CBOR representation.
Collections of `std::byte`, including byte-valued sets, retain the library's
byte-string representation. Their normal insertion semantics still apply.
Singly linked lists append in wire order, after any existing elements. The
decoder finds the destination tail once per decoded array; it does not prewalk
the input.

## Coverage

The integration tests use ordinary integer elements/keys/values and default
container policies. Support still requires encodable/decodable element types,
truthful range semantics, sufficient capacity, and appropriate allocators.

| Family | Use | Validation |
| --- | --- | --- |
| Standard ordered/unordered `set` and `multiset` | Direct CBOR array | C++20, GCC 16/libstdc++ 16 |
| Boost.Container `vector`, `deque`, `list`, `stable_vector`, `small_vector`, `static_vector`, `devector` | Direct CBOR array | Boost 1.90 and 1.92 |
| Boost.Container `slist` | Direct CBOR array, ordered append | Boost 1.90 and 1.92 |
| Boost.Container `set`, `multiset`, `flat_set`, `flat_multiset` | Direct CBOR array | Boost 1.90 and 1.92 |
| Boost.Unordered `unordered_set`, `unordered_multiset`, `unordered_flat_set`, `unordered_node_set` | Direct CBOR array | Boost 1.90 and 1.92 |
| `boost::array<T, N>` | Direct fixed array (`std::byte` elements use a byte string) | Boost 1.90 and 1.92; zero and nonzero extents |
| Boost.Container `string` | Direct text string | Boost 1.90 and 1.92 |
| Boost.Container `map`, `multimap`, `flat_map`, `flat_multimap` | Direct CBOR map | Boost 1.90 and 1.92 |
| Boost.Unordered `unordered_map`, `unordered_multimap`, `unordered_flat_map`, `unordered_node_map` | Direct CBOR map | Boost 1.90 and 1.92 |
| Boost.Container `hub`, `segtor` | Direct CBOR array | Boost 1.92 |
| Standard `flat_set`, `flat_multiset`, `flat_map`, `flat_multimap` | Direct array/map | C++26 mode, libstdc++ 16; gated on C++23 library macros |
| Standard `inplace_vector` | Direct CBOR array, within capacity | C++26, libstdc++ 16 |
| Standard `hive` | Insert-based array path | Feature-gated test present; unavailable in the local libstdc++ 16 installation |
| `boost::shared_ptr<T>`, `boost::movelib::unique_ptr<T>` | Existing opt-in `cbor::tags::codec::shared_ptr` / `cbor::tags::codec::unique_ptr` | Ordinary integer and null values; no new graph-identity claim |

The 28 Boost container families above have executable coverage. Boost is an
optional **test** dependency, with no new public build/link dependency. The
container CI workflow downloads pinned Boost 1.92 headers and requires that
`hub`/`segtor` checks compile. Configuration reports newer standard-library
features individually; an unavailable feature is not evidence of support.
For a local equivalent:

```sh
cmake --preset=debug-cxx26 \
  -DCBOR_TAGS_TEST_BOOST_ROOT=/path/to/boost_1_92_0 \
  -DCBOR_TAGS_TEST_REQUIRE_RECENT_BOOST=ON
cmake --build --preset=debug-cxx26 --parallel
ctest --preset=debug-cxx26 --output-on-failure
```

Fixed arrays require an exact definite length. Indefinite input retains the
existing fixed-array `unexpected_group_size` policy. A `std::span<T, N>` over a
Boost array remains a useful explicit adapter. General C-array members in
reflected aggregates are a separate concern (#133).
Character arrays and mutable character spans retain the existing integer-array
representation. Use a string for owning text, or `std::span<const char, N>` for
a fixed-size borrowed text view. Decoding a const span borrows the input bytes;
it does not fill character-array storage.

Fixed-capacity vectors and circular buffers need an admission bound when
incoming arrays could exceed remaining capacity. Use the existing
`as_bounded_size(out, 0, out.capacity() - out.size())` wrapper; see
[decoder resource limits](decoder_resource_limits.md). A direct circular-buffer
decode uses its normal overwrite policy. Failures are terminal, and an accepted
prefix can remain. No retry, rollback, or input prewalk is introduced.

## Concurrency and lifetime

Concurrent containers generally expose visitation rather than an ordinary
stable range. Serialize through an application-controlled snapshot or visitation
protocol, with synchronization appropriate to that API. The library does not
invent locking or snapshot consistency. Hazard pointers and RCU protect
lifetimes/reclamation; their guard/domain objects are not serialized containers.
The application must keep the observed data valid and consistently readable
for the entire encode/decode operation.

References: [C++26 draft N5050](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/n5050.pdf),
[Boost 1.92 hub](https://github.com/boostorg/container/blob/boost-1.92.0/include/boost/container/hub.hpp),
[Boost 1.92 segtor](https://github.com/boostorg/container/blob/boost-1.92.0/include/boost/container/segtor.hpp).
