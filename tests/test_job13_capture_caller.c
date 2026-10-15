#define _CRT_SECURE_NO_WARNINGS
#include "haze_job13_capture.h"

/* This translation unit models the generated image's C ABI. Keep the calls
 * here so the focused link catches accidental C++ name mangling. */
bool job13_c_begin(spu_context* ctx) {
    return haze_job13_capture_begin(ctx);
}

void job13_c_end(spu_context* ctx) {
    haze_job13_capture_end(ctx);
}

void job13_c_wrch_word(spu_context* ctx, uint32_t channel, uint32_t word) {
    u128 value = {0};
    value._u32[0] = word;
    haze_job13_capture_wrch(ctx, channel, value);
}

uint32_t job13_c_rdch_word(spu_context* ctx, uint32_t channel) {
    const u128 value = haze_job13_capture_rdch(ctx, channel);
    return value._u32[0];
}

uint32_t job13_c_rchcnt(spu_context* ctx, uint32_t channel) {
    return haze_job13_capture_rchcnt(ctx, channel);
}
