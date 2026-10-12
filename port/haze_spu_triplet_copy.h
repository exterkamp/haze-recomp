#pragma once
#include "spu_context.h"

#ifdef __cplusplus
extern "C" {
#endif
int haze_spu_triplet_copy_enabled(void);
void haze_spu_triplet_copy_note(unsigned items);
#ifdef __cplusplus
}
#endif

/* Image 42, LS 35F8: copy three aligned words into a 2048-entry ring.
 * Leave the final iteration to the original lift to reproduce ALL scratch
 * registers, including nonpreferred lanes. Preserve word-by-word ordering
 * for overlapping buffers; memcpy of the whole range is not equivalent.
 * No channels, tags, task completion or translated control transfers change.
 */
static inline unsigned haze_spu_triplet_copy(spu_context* ctx)
{
    if (ctx->image_id != 42 || ctx->pc != 0x35F8 ||
        ctx->ls != ctx->ls_store || ctx->policy_mode ||
        g_spu_ls_watch_n != 0 || g_spu_ls_probe != 0 || g_spu_smc_watch != 0)
        return 0;
    const uint32_t first = ctx->gpr[15]._u32[0];
    const uint32_t count = ctx->gpr[16]._u32[0] - first;
    const uint32_t source = ctx->gpr[18]._u32[0];
    const uint32_t target = ctx->gpr[14]._u32[0];
    const uint32_t ring_start = ctx->gpr[17]._u32[0];
    if (count < 2 || count > 2048 || ((source | target) & 3) ||
        ctx->gpr[20]._u32[0] != 0xFFFF || ctx->gpr[21]._u32[0] != 2047)
        return 0;
    const uint32_t skip = count - 1;
    for (uint32_t i = 0; i < skip; ++i) {
        const uint32_t index = first + i;
        const uint32_t src = source + 12 * (index & 0xFFFF);
        const uint32_t dst = target + 12 * ((ring_start + index) & 2047);
        for (unsigned word = 0; word != 3; ++word) {
            uint32_t bits;
            memcpy(&bits, ctx->ls + ((src + word * 4) & SPU_LS_MASK), 4);
            memcpy(ctx->ls + ((dst + word * 4) & SPU_LS_MASK), &bits, 4);
        }
    }
    for (unsigned lane = 0; lane != 4; ++lane)
        ctx->gpr[15]._u32[lane] += skip;
    return skip;
}
