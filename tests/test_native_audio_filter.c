/* Actual generated PIC04 loop versus the bounded whole-loop prototype.
 * The Python driver supplies guarded private-source includes and the SDK. */
#include "spu_helpers.h"
#include "haze_audio_filter.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
int g_spu_ls_watch_n=0;
void spu_ls_watch_slow(uint32_t a,int b,const uint8_t* c,uint32_t d,uint32_t e) {
    (void)a;(void)b;(void)c;(void)d;(void)e; abort();
}
void audio_pic_04_spu_func_0002FA78(spu_context* ctx) { (void)ctx; abort(); }
#include "audio_filter_original.generated.h"
static u128 oracle_mul(u128 a,u128 b) {
    u128 r;for(unsigned i=0;i<4;++i)r._u32[i]=spu_float_mul(a._u32[i],b._u32[i]);return r;
}
static u128 oracle_madd(u128 a,u128 b,u128 c) {
    u128 r;for(unsigned i=0;i<4;++i)r._u32[i]=spu_float_madd(a._u32[i],b._u32[i],c._u32[i]);return r;
}
#define audio_pic_04_spu_func_0002FA40 scalar_original
#define spu_fm oracle_mul
#define spu_fma oracle_madd
#include "audio_filter_original.generated.h"
#undef spu_fma
#undef spu_fm
#undef audio_pic_04_spu_func_0002FA40
static uint32_t seed=0x14104A40;
static uint32_t random_word(void) { seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed; }
static void seed_case(spu_context* ctx,unsigned n,unsigned count) {
    memset(ctx,0,sizeof(*ctx));
    ctx->ls=ctx->ls_store;
    for(unsigned r=0;r<128;++r) for(unsigned l=0;l<4;++l) ctx->gpr[r]._u32[l]=random_word();
    for(unsigned i=0;i<SPU_LS_SIZE;i+=4) {
        uint32_t word=n&1 ? random_word() : 0x3D000000u|(random_word()&0xFFFFFFu);
        word=SPU_BSWAP32(word);memcpy(ctx->ls+i,&word,4);
    }
    ctx->image_id=51; ctx->pc=0x2FA40; ctx->host_depth=3;
    ctx->gpr[9]._u32[0]=n&2 ? UINT32_MAX-3u : random_word();
    ctx->gpr[23]._u32[0]=ctx->gpr[9]._u32[0]+count;
    ctx->gpr[8]._u32[0]=(n%3==0) ? SPU_LS_SIZE-9u : n%3==1 ? UINT32_MAX-15u : 0x1003;
    ctx->gpr[24]._u32[0]=n&4 ? UINT32_MAX-15u : 0;
    ctx->gpr[10]._u32[0]=ctx->gpr[24]._u32[0]+(n%3==0 ? 0u : n%3==1 ? 16u : UINT32_MAX-15u);
    const uint32_t edges[]={0,1,0x007FFFFF,0x00800000,0x3F800000,0x7EFFFFFF,
        0x7F000000,0x7F800000,0x7FFFFFFF,0x80000000,0xBF800001,0xFFFFFFFF};
    for(unsigned l=0;l<4;++l) {
        if(n&1) {
            ctx->gpr[20]._u32[l]=edges[(n+l)%12];
            ctx->gpr[11]._u32[l]=edges[(n+3+l)%12];
            ctx->gpr[13]._u32[l]=edges[(n+6+l)%12];
        } else {
            ctx->gpr[20]._f32[l]=0.5f;ctx->gpr[11]._f32[l]=0.25f;ctx->gpr[13]._f32[l]=0.125f;
        }
    }
    for(unsigned b=0;b<16;++b) {
        ctx->gpr[14]._u8[b]=(uint8_t)(random_word()&31u);
        ctx->gpr[12]._u8[b]=(uint8_t)(n&8 ? random_word() : (b+4)&31u);
    }
}
static void copy_context(spu_context* destination,const spu_context* source) {
    memcpy(destination,source,sizeof(*source));destination->ls=destination->ls_store;
}
static int equal_context(const spu_context* a,spu_context* b) {
    /* Private storage pointers naturally differ; compare their meaning plus
     * every other context byte, including the complete owned LS backing. */
    assert(a->ls==a->ls_store && b->ls==b->ls_store);
    uint8_t* saved=b->ls;b->ls=a->ls;
    const int same=!memcmp(a,b,sizeof(*a));b->ls=saved;return same;
}
static void unchanged_reject(spu_context* a,spu_context* b,int permitted) {
    copy_context(b,a);
    g_spu_trampoline_fn=audio_pic_04_spu_func_0002FA78;
    assert(!haze_audio_filter_try_native(a,audio_pic_04_spu_func_0002FA78,permitted));
    assert(equal_context(a,b) && g_spu_trampoline_fn==audio_pic_04_spu_func_0002FA78);
}
static ULONG64 cycles(void) {
    ULONG64 out=0;assert(QueryThreadCycleTime(GetCurrentThread(),&out));return out;
}
static void benchmark(spu_context* a,spu_context* b) {
    /* CPU cycles for this loop only; excludes decoder, scheduling and frames.
     * Identical stable input, five alternating pairs, initialization untimed. */
    for(unsigned run=0;run<5;++run) {
        ULONG64 elapsed[2];
        const uint32_t pair_seed=seed;
        for(unsigned order=0;order<2;++order) {
            const unsigned native=(run+order)&1u;
            spu_context* ctx=native ? b : a;
            seed=pair_seed;seed_case(ctx,0,64);
            const unsigned repetitions=20000;
            ULONG64 begin=cycles();
            for(unsigned i=0;i<repetitions;++i) {
                ctx->gpr[9]._u32[0]=0;ctx->gpr[23]._u32[0]=64;
                ctx->gpr[8]._u32[0]=0x1000;
                if(native) assert(haze_audio_filter_try_native(ctx,audio_pic_04_spu_func_0002FA78,1));
                else audio_pic_04_spu_func_0002FA40(ctx);
            }
            elapsed[native]=cycles()-begin;
        }
        printf("loop_microbench pair=%u vectors=1280000 original_cycles=%llu native_cycles=%llu ratio=%.6f\n",
            run,(unsigned long long)elapsed[0],(unsigned long long)elapsed[1],(double)elapsed[1]/(double)elapsed[0]);
    }
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    setvbuf(stdout,NULL,_IONBF,0);
    spu_context* a=(spu_context*)malloc(sizeof(*a));
    spu_context* b=(spu_context*)malloc(sizeof(*b));assert(a && b);
    spu_context* c=(spu_context*)malloc(sizeof(*c));assert(c);
    const int supported=spu_float_native_available();
    printf("native_filter CPU_OS_supported=%d\n",supported);
    seed_case(a,0,1);unchanged_reject(a,b,0);
    assert(!haze_audio_filter_try_native(NULL,audio_pic_04_spu_func_0002FA78,1));
    assert(!haze_audio_filter_try_native(a,NULL,1));
    assert(!spu_float_native_set_mode(0));unchanged_reject(a,b,1);
    if(!supported) { puts("PASS disabled/unsupported fallback; native execution unavailable");free(a);free(b);free(c);return 0; }
    assert(spu_float_native_set_mode(1));
    if(argc>1 && !strcmp(argv[1],"--audit-mode")) {
        unchanged_reject(a,b,1);puts("PASS audit mode retains original SDK validation path");free(a);free(b);free(c);return 0;
    }
    seed_case(a,0,0);unchanged_reject(a,b,1);
    seed_case(a,0,4097);unchanged_reject(a,b,1);
    const unsigned saved=_mm_getcsr();unsigned checks=0;
    for(unsigned mode=0;mode<8;++mode) {
        _mm_setcsr((saved&~(0x6000u|0x8040u))|((mode&3u)<<13)|(mode&4u?0x8040u:0));
        for(unsigned n=0;n<96;++n) {
            seed_case(a,n,n%4==0 ? 1u : n%4==1 ? 2u : n%4==2 ? 31u : 128u);
            copy_context(b,a);
            copy_context(c,a);
            const unsigned before=_mm_getcsr();
            g_spu_trampoline_fn=NULL;scalar_original(a);
            assert(g_spu_trampoline_fn==audio_pic_04_spu_func_0002FA78);
            g_spu_trampoline_fn=NULL;audio_pic_04_spu_func_0002FA40(c);
            assert(g_spu_trampoline_fn==audio_pic_04_spu_func_0002FA78 && equal_context(a,c));
            g_spu_trampoline_fn=NULL;
            assert(haze_audio_filter_try_native(b,audio_pic_04_spu_func_0002FA78,1));
            if(!equal_context(a,b)) { fprintf(stderr,"FAIL case=%u mxcsr_mode=%u\n",n,mode);abort(); }
            assert(g_spu_trampoline_fn==audio_pic_04_spu_func_0002FA78 && _mm_getcsr()==before);
            ++checks;
        }
    }
    _mm_setcsr(saved);
    seed_case(a,6,4096);copy_context(b,a);copy_context(c,a);
    scalar_original(a);audio_pic_04_spu_func_0002FA40(c);
    assert(haze_audio_filter_try_native(b,audio_pic_04_spu_func_0002FA78,1));
    assert(equal_context(a,b) && equal_context(a,c) && _mm_getcsr()==saved);++checks;
    for(unsigned block=0;block<16;++block) {
        /* Keep history and output alive across calls, as the streamed DSP
         * does, while rearming only the loop counter in each context. */
        a->gpr[9]._u32[0]=b->gpr[9]._u32[0]=c->gpr[9]._u32[0]=0;
        a->gpr[23]._u32[0]=b->gpr[23]._u32[0]=c->gpr[23]._u32[0]=8;
        scalar_original(a);audio_pic_04_spu_func_0002FA40(c);
        assert(haze_audio_filter_try_native(b,audio_pic_04_spu_func_0002FA78,1));
        assert(equal_context(a,b) && equal_context(a,c) && _mm_getcsr()==saved);++checks;
    }
    printf("PASS %u complete-context/LS/continuation differential cases; alias, wrap, counter overflow, extreme lanes, eight MXCSR modes\n",checks);
    if(argc>1 && !strcmp(argv[1],"--benchmark")) benchmark(a,b);
    free(a);free(b);free(c);return 0;
}
