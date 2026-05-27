# Variant Handling

## Runtime Decode

`std::variant` decode dispatches by the current CBOR major type and, for tag
values, by the decoded tag number. Nested variants participate in the same
dispatch instead of being treated as an opaque alternative:

```cpp
using nested_type = std::variant<cbor::tags::static_tag<42>, std::string>;
using value_type = std::variant<std::uint64_t, nested_type>;

auto input = to_bytes("d82a"); // #6.42
value_type value{};
auto dec = cbor::tags::make_decoder(input);

REQUIRE(dec(value));
REQUIRE(std::holds_alternative<nested_type>(value));
CHECK(std::holds_alternative<cbor::tags::static_tag<42>>(std::get<nested_type>(value)));
```

Alternative shapes must still be unambiguous. A variant with two alternatives
that both match the same tag or the same catch-all shape is rejected at compile
time where the type system exposes that collision.

## Non-Std Variants

Only `std::variant` is recognized automatically. Other union types must opt in
with `cbor::tags::variant_traits`; structural `index()`/`visit()` lookalikes are
not treated as variants by default. Without the specialization, normal overload
resolution applies, so an aggregate wrapper may encode as an aggregate.

```cpp
#include <boost/variant2/variant.hpp>

template <class... Ts>
struct cbor::tags::variant_traits<boost::variant2::variant<Ts...>> {
    using variant_type = boost::variant2::variant<Ts...>;

    static constexpr std::size_t size =
        boost::variant2::variant_size_v<variant_type>;

    template <std::size_t I>
    using alternative =
        boost::variant2::variant_alternative_t<I, variant_type>;

    static std::size_t index(variant_type const& value) {
        return value.index();
    }

    template <std::size_t I, class V>
    static decltype(auto) get(V&& value) {
        return boost::variant2::get<I>(std::forward<V>(value));
    }

    template <class Visitor, class... Vs>
    static decltype(auto) visit(Visitor&& visitor, Vs&&... values) {
        return boost::variant2::visit(std::forward<Visitor>(visitor),
                                      std::forward<Vs>(values)...);
    }

    template <std::size_t I, class U>
    static void assign(variant_type& value, U&& decoded) {
        value.template emplace<I>(std::forward<U>(decoded));
    }
};
```

`visit` must support both single-variant and multi-variant calls because
encoding uses the former and comparison helpers use the latter.

For raw `union` storage with a domain-specific discriminator, prefer an ADL
`encode`/`decode` overload unless the type can honestly expose the same
`index`/`get`/`visit`/`assign` operations as a variant.

## Compile-Time Dispatch

Below is "pseudo code" that can be investigated with [godbolt](https://godbolt.org/). The code shows how you can optimize away unused code paths with `if constexpr`; in this case the code paths are constrained by the variant type.

Allocator note: `std::variant` does not carry a parent container allocator. Decoding `std::pmr` alternatives inside a variant nested in a PMR container is a known allocator-containment gap until variant alternative construction gets an explicit parent allocator context.

```cpp

#include <type_traits>
#include <variant>
#include <vector>
#include <cassert>
#include <stdexcept>

// All possible types
struct Type1 {};
struct Type2 {};
struct Type3 {};
struct Type4 {};

// Helper to check if type is in variant
template <typename T, typename Variant> struct is_variant_member;

template <typename T, typename... Types>
struct is_variant_member<T, std::variant<Types...>> : std::bool_constant<(std::is_same_v<T, Types> || ...)> {};

template <typename T, typename Variant> inline constexpr bool is_variant_member_v = is_variant_member<T, Variant>::value;

// Parser for specific types
template <typename T, typename Buffer> T parse(Buffer &) { return T{}; }

using Buffer = std::vector<std::byte>;

template <typename... AllowedTypes> constexpr auto parseByID(std::variant<AllowedTypes...> &var, int typeID, Buffer &buf) -> bool {
    using Variant = std::variant<AllowedTypes...>;

    switch (typeID) {
    case 1:
        if constexpr (is_variant_member_v<Type1, Variant>) {
            var = parse<Type1>(buf);
            return true;
        }
        break;
    case 2:
        if constexpr (is_variant_member_v<Type2, Variant>) {
            var = parse<Type2>(buf);
            return true;
        }
        break;
    case 3:
        if constexpr (is_variant_member_v<Type3, Variant>) {
            var = parse<Type3>(buf);
            return true;
        }
        break;
    case 4:
        if constexpr (is_variant_member_v<Type4, Variant>) {
            var = parse<Type4>(buf);
            return true;
        }
        break;
    default: break;
    }
    return false;
}

int main() {
    // Variant member check
    using var = std::variant<Type1, Type2, Type3>;
    static_assert(is_variant_member_v<Type1, var>);
    static_assert(is_variant_member_v<Type2, var>);
    static_assert(is_variant_member_v<Type3, var>);
    static_assert(!is_variant_member_v<Type4, var>);

    // ParseByID tests
    using Variant = std::variant<Type1, Type2>;
    Buffer buf;
    Variant val1;

    parseByID(val1, 1, buf);
    assert(std::holds_alternative<Type1>(val1));

    parseByID(val1, 2, buf);
    assert(std::holds_alternative<Type2>(val1));

    assert(!parseByID(val1, 3, buf));
    assert(!parseByID(val1, 4, buf));
    assert(!parseByID(val1, 5, buf));

    return 0;
}

```

Below we have optimized assembly, note that comparing of types Type3 and Type4 does not exist.

```asm

main:
        push    rbx
        sub     rsp, 16
        lea     rbx, [rsp + 12]
        mov     byte ptr [rbx + 1], 0
        lea     rsi, [rsp + 14]
        mov     rdi, rbx
        call    std::enable_if<__exactly_once<std::_Nth_type<__accepted_index<Type1&&>, Type1, Type2>::type> && is_constructible_v<std::_Nth_type<__accepted_index<Type1&&>, Type1, Type2>::type, Type1> && is_assignable_v<std::_Nth_type<__accepted_index<Type1&&>, Type1, Type2>::type&, Type1>, std::variant<Type1, Type2>&>::type std::variant<Type1, Type2>::operator=<Type1>(Type1&&)
        cmp     byte ptr [rbx + 1], 0
        jne     .LBB0_3
        lea     rbx, [rsp + 12]
        lea     rsi, [rsp + 15]
        mov     rdi, rbx
        call    std::enable_if<__exactly_once<std::_Nth_type<__accepted_index<Type2&&>, Type1, Type2>::type> && is_constructible_v<std::_Nth_type<__accepted_index<Type2&&>, Type1, Type2>::type, Type2> && is_assignable_v<std::_Nth_type<__accepted_index<Type2&&>, Type1, Type2>::type&, Type2>, std::variant<Type1, Type2>&>::type std::variant<Type1, Type2>::operator=<Type2>(Type2&&)
        cmp     byte ptr [rbx + 1], 1
        jne     .LBB0_4
        xor     eax, eax
        add     rsp, 16
        pop     rbx
        ret
.LBB0_3:
        lea     rdi, [rip + .L.str]
        lea     rsi, [rip + .L.str.1]
        lea     rcx, [rip + .L__PRETTY_FUNCTION__.main]
        mov     edx, 73
        call    __assert_fail@PLT
.LBB0_4:
        lea     rdi, [rip + .L.str.2]
        lea     rsi, [rip + .L.str.1]
        lea     rcx, [rip + .L__PRETTY_FUNCTION__.main]
        mov     edx, 76
        call    __assert_fail@PLT

__clang_call_terminate:
        push    rax
        call    __cxa_begin_catch@PLT
        call    std::terminate()@PLT

.L.str:
        .asciz  "std::holds_alternative<Type1>(val1)"

.L.str.1:
        .asciz  "/app/example.cpp"

.L__PRETTY_FUNCTION__.main:
        .asciz  "int main()"

.L.str.2:
        .asciz  "std::holds_alternative<Type2>(val1)"

.L.str.4:
        .asciz  "std::get: wrong index for variant"

typeinfo name for std::bad_variant_access:
        .asciz  "St18bad_variant_access"

typeinfo for std::bad_variant_access:
        .quad   vtable for __cxxabiv1::__si_class_type_info+16
        .quad   typeinfo name for std::bad_variant_access
        .quad   typeinfo for std::exception

vtable for std::bad_variant_access:
        .quad   0
        .quad   typeinfo for std::bad_variant_access
        .quad   std::exception::~exception() [base object destructor]
        .quad   std::bad_variant_access::~bad_variant_access() [deleting destructor]
        .quad   std::bad_variant_access::what() const

DW.ref.__gxx_personality_v0:
        .quad   __gxx_personality_v0

```

It can be taken a step further in this specific example with gcc 14.2 (-Os flag) to eliminate all code.

```asm
main:
        xor     eax, eax
        ret
typeinfo name for std::bad_variant_access:
        .string "St18bad_variant_access"
typeinfo for std::bad_variant_access:
        .quad   vtable for __cxxabiv1::__si_class_type_info+16
        .quad   typeinfo name for std::bad_variant_access
        .quad   typeinfo for std::exception
vtable for std::bad_variant_access:
        .quad   0
        .quad   typeinfo for std::bad_variant_access
        .quad   std::bad_variant_access::~bad_variant_access() [complete object destructor]
        .quad   std::bad_variant_access::~bad_variant_access() [deleting destructor]
        .quad   std::bad_variant_access::what() const
```
