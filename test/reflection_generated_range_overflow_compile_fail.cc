#include <cbor_tags/cbor_reflection.h>

struct twenty_five_required_references {
    int &p01;
    int &p02;
    int &p03;
    int &p04;
    int &p05;
    int &p06;
    int &p07;
    int &p08;
    int &p09;
    int &p10;
    int &p11;
    int &p12;
    int &p13;
    int &p14;
    int &p15;
    int &p16;
    int &p17;
    int &p18;
    int &p19;
    int &p20;
    int &p21;
    int &p22;
    int &p23;
    int &p24;
    int &p25;
};

int main() {
    int                             value{};
    twenty_five_required_references object{value, value, value, value, value, value, value, value, value, value, value, value, value,
                                           value, value, value, value, value, value, value, value, value, value, value, value};
    (void)cbor::tags::to_tuple(object);
}
