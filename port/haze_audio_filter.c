#include "haze_audio_filter.h"
#include "spu_helpers.h"
#include <stdlib.h>
#if defined(SPU_FLOAT_NATIVE_HOST)
#include <windows.h>
#include <immintrin.h>
/* Generated from the exact SDK arithmetic implementation by the oracle
 * preparer. No independently maintained arithmetic lives in this prototype.
 * A reusable SDK core header is required before production integration. */
#include "haze_audio_filter_kernels.generated.h"
static INIT_ONCE filter_once=INIT_ONCE_STATIC_INIT;
static int filter_validation;
static BOOL CALLBACK filter_initialize(PINIT_ONCE once, PVOID param, PVOID* out) {
    (void)once; (void)param; (void)out;
    const char* value=getenv("SPU_FLOAT_NATIVE_VALIDATE");
    filter_validation=value && *value=='1';
    return TRUE;
}
static inline __attribute__((always_inline,target("avx512f")))
u128 filter_mul(u128 a,u128 b) {
    u128 r; _mm_storeu_ps(r._f32,haze_filter_mul_ps(
        _mm_loadu_ps(a._f32),_mm_loadu_ps(b._f32))); return r;
}
static inline __attribute__((always_inline,target("avx512f")))
u128 filter_madd(u128 a,u128 b,u128 c) {
    u128 r; _mm_storeu_ps(r._f32,haze_filter_madd_ps(
        _mm_loadu_ps(a._f32),_mm_loadu_ps(b._f32),_mm_loadu_ps(c._f32))); return r;
}
static __attribute__((target("avx512f"))) void filter_block(
    spu_context* ctx,void (*continuation)(spu_context*)) {
    /* All architecturally modified registers are published at the boundary.
     * Reads/writes retain the real LS masking, endian and watch helpers; the
     * load precedes the store on every iteration, including overlapping spans. */
    u128 r9=ctx->gpr[9],r8=ctx->gpr[8],r7=ctx->gpr[7];
    const u128 r24=ctx->gpr[24],r23=ctx->gpr[23],r20=ctx->gpr[20];
    const u128 r14=ctx->gpr[14],r12=ctx->gpr[12],r11=ctx->gpr[11];
    const u128 r13=ctx->gpr[13],r10=ctx->gpr[10];
    u128 r66,r61,r65,r64,r62,r63,r4;
    do {
        r9=spu_ai(r9,1);
        r66=spu_ls_read128(ctx,r8._u32[0]+r24._u32[0]);
        r61=spu_ceq(r23,r9);
        r65=filter_mul(r66,r20);
        r64=spu_shufb(r66,r7,r14);
        r62=spu_shufb(r66,r7,r12);
        r7=r66;
        r63=filter_madd(r64,r11,r65);
        r4=filter_madd(r62,r13,r63);
        spu_ls_write128(ctx,r8._u32[0]+r10._u32[0],r4);
        r8=spu_ai(r8,16);
    } while(r61._u32[0]==0);
    ctx->gpr[9]=r9; ctx->gpr[8]=r8; ctx->gpr[7]=r7;
    ctx->gpr[66]=r66; ctx->gpr[61]=r61; ctx->gpr[65]=r65;
    ctx->gpr[64]=r64; ctx->gpr[62]=r62; ctx->gpr[63]=r63; ctx->gpr[4]=r4;
    ctx->pc=0x2FA78; g_spu_trampoline_fn=continuation;
}
#endif
int haze_audio_filter_try_native(spu_context* ctx,
    void (*continuation)(spu_context*),int permitted) {
#if defined(SPU_FLOAT_NATIVE_HOST)
    if(!ctx || !continuation || !permitted || !spu_float_native_active() ||
        !spu_float_native_available()) return 0;
    InitOnceExecuteOnce(&filter_once,filter_initialize,NULL,NULL);
    /* Preserve the SDK's per-operation audit/failure latch by using the
     * original path whenever its diagnostic is enabled. */
    if(filter_validation) return 0;
    const uint32_t iterations=ctx->gpr[23]._u32[0]-ctx->gpr[9]._u32[0];
    if(!iterations || iterations>4096u) return 0;
    filter_block(ctx,continuation); return 1;
#else
    (void)ctx; (void)continuation; (void)permitted; return 0;
#endif
}
