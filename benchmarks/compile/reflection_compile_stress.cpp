#include <cbor_tags/cbor_reflection.h>
#include <cstddef>
#include <tuple>
#include <utility>

#ifndef CBOR_TAGS_REFLECTION_BENCH_INSTANCES
#define CBOR_TAGS_REFLECTION_BENCH_INSTANCES 512
#endif

#ifndef CBOR_TAGS_REFLECTION_BENCH_MUTABLE_REFERENCES
#define CBOR_TAGS_REFLECTION_BENCH_MUTABLE_REFERENCES 0
#endif

#ifndef CBOR_TAGS_REFLECTION_BENCH_EXPECTED_ARITY
#define CBOR_TAGS_REFLECTION_BENCH_EXPECTED_ARITY 8
#endif

template <std::size_t> struct value_aggregate {
    int p1;
    int p2;
    int p3;
    int p4;
    int p5;
    int p6;
    int p7;
    int p8;
};

template <std::size_t> struct mutable_reference_aggregate {
    int  p1;
    int  p2;
    int  p3;
    int  p4;
    int  p5;
    int  p6;
    int  p7;
    int &p8;
};

#if CBOR_TAGS_REFLECTION_BENCH_MUTABLE_REFERENCES
template <std::size_t I> using benchmark_type = mutable_reference_aggregate<I>;
#else
template <std::size_t I> using benchmark_type = value_aggregate<I>;
#endif

template <std::size_t I>
constexpr std::size_t reflected_size = std::tuple_size_v<decltype(cbor::tags::to_tuple(std::declval<benchmark_type<I> &>()))>;

template <std::size_t... Is> consteval std::size_t instantiate_all(std::index_sequence<Is...>) { return (reflected_size<Is> + ... + 0U); }

constexpr auto reflected_total = instantiate_all(std::make_index_sequence<CBOR_TAGS_REFLECTION_BENCH_INSTANCES>{});
static_assert(reflected_total == CBOR_TAGS_REFLECTION_BENCH_INSTANCES * CBOR_TAGS_REFLECTION_BENCH_EXPECTED_ARITY);

int main() { return reflected_total == 0U; }
