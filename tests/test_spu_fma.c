#include "../external/ps3recomp/runtime/spu/spu_helpers.h"
#include <assert.h>

// Indirect calls keep the scalar CRT reference from becoming the same
// hardware instructions as the optimized helper under test.
static float (*volatile reference_fma)(float, float, float) = fmaf;
static uint32_t random_bits = 0x51423411;
static uint32_t next_bits(void) {
    random_bits ^= random_bits << 13;
    random_bits ^= random_bits >> 17;
    random_bits ^= random_bits << 5;
    return random_bits;
}
static void compare(float expected, float actual) {
    uint32_t a, b;
    memcpy(&a, &expected, 4); memcpy(&b, &actual, 4);
    assert((isnan(expected) && isnan(actual)) || a == b);
}
int main(void) {
    for (unsigned n = 0; n < 100000; ++n) {
        u128 a, b, c;
        for (unsigned lane = 0; lane < 4; ++lane) {
            a._u32[lane] = next_bits();
            b._u32[lane] = next_bits();
            c._u32[lane] = next_bits();
        }
        u128 add = spu_fma(a,b,c), sub = spu_fms(a,b,c), neg = spu_fnms(a,b,c);
        for (unsigned lane = 0; lane < 4; ++lane) {
            compare(reference_fma(a._f32[lane], b._f32[lane], c._f32[lane]), add._f32[lane]);
            compare(reference_fma(a._f32[lane], b._f32[lane], -c._f32[lane]), sub._f32[lane]);
            compare(reference_fma(a._f32[lane], -b._f32[lane], c._f32[lane]), neg._f32[lane]);
        }
    }
    puts("SPU FMA/FMS/FNMS: 1,200,000 lanes match scalar fused rounding.");
}
