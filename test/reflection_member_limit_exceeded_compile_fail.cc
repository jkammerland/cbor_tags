#include <cbor_tags/cbor_reflection.h>

// 25 plain value members: one past MAX_REFLECTION_MEMBERS.
//
// Unlike reflection_generated_range_overflow_compile_fail.cc, whose reference
// members cannot be value-initialized and so match no generated arity at all,
// this aggregate IS brace-constructible from 24 probes (the tail is simply
// value-initialized). The descending scan therefore selects the 24-member
// branch and the failure surfaces inside the structured binding, with nothing
// pointing at CBOR_TAGS_REFLECTION_RANGES, unless the member-count guard fires
// first.
struct twenty_five_values {
    int p01;
    int p02;
    int p03;
    int p04;
    int p05;
    int p06;
    int p07;
    int p08;
    int p09;
    int p10;
    int p11;
    int p12;
    int p13;
    int p14;
    int p15;
    int p16;
    int p17;
    int p18;
    int p19;
    int p20;
    int p21;
    int p22;
    int p23;
    int p24;
    int p25;
};

int main() {
    twenty_five_values object{};
    (void)cbor::tags::to_tuple(object);
}
