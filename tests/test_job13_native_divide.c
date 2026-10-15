/* Compare against actual original EB10/EB24/EB38 code. Private fixture supplies
 * image constants only; no original game operations are copied into this test. */
#define main original_replay_main
#include "replay_job13_capture.c"
#undef main
#include "spu_helpers.h"
#include "haze_job13_native_divide.h"

static uint64_t rng_state=UINT64_C(0xbad5eed1960708);
static uint32_t random_word(void) {
    rng_state^=rng_state<<13;rng_state^=rng_state>>7;rng_state^=rng_state<<17;
    return (uint32_t)rng_state;
}
static void finish_division(spu_context* ctx) {
    unsigned transfers=0;
    while(ctx->pc!=0xEA38) {
        void (*fn)(spu_context*)=g_spu_trampoline_fn;
        require(fn!=NULL && ++transfers<130,"division escaped bounded block region");
        g_spu_trampoline_fn=NULL;fn(ctx);
    }
    g_spu_trampoline_fn=NULL;
}

int main(int argc,char **argv) {
    require(argc==2,"expected valid private fixture");
    FILE *f=fopen(argv[1],"rb");require(f!=NULL,"open fixture");
    read_exact(f,&header,sizeof header);
    require(header.valid==1 && header.version==2 && header.image_id==32,"invalid fixture");
    spu_context *a=(spu_context*)_aligned_malloc(sizeof *a,16);
    spu_context *b=(spu_context*)_aligned_malloc(sizeof *b,16);
    uint8_t *ls=(uint8_t*)malloc(SPU_LS_SIZE);
    require(a && b && ls,"allocate contexts");
    require(!fseek(f,128+4096,SEEK_SET),"seek constants");read_exact(f,ls,SPU_LS_SIZE);fclose(f);
    spu_0031_at_00ECEC00_spu_recomp_register();
    for (unsigned trial=0;trial<10000;++trial) {
        spu_context_init(a,0);a->image_id=32;
        memcpy(a->ls,ls,SPU_LS_SIZE);
        for(unsigned r=0;r<128;++r) for(unsigned lane=0;lane<4;++lane)
            a->gpr[r]._u32[lane]=random_word();
        a->gpr[1]=spu_splat_u32(0x3c000);
        if(trial<16) {
            uint64_t edge[]={0,1,UINT64_MAX,UINT64_C(1)<<63};
            uint64_t d=edge[trial%4],n=edge[trial/4];
            a->gpr[8]._u32[0]=(uint32_t)(d>>32);a->gpr[8]._u32[1]=(uint32_t)d;
            a->gpr[9]._u32[0]=(uint32_t)(n>>32);a->gpr[9]._u32[1]=(uint32_t)n;
        }
        u128 gt=spu_clgt(a->gpr[8],a->gpr[9]);
        a->gpr[5]=spu_selb(gt,spu_xswd(gt),spu_ceq(a->gpr[8],a->gpr[9]));
        memcpy(b,a,sizeof *a);b->ls=b->ls_store;
        steps=0;g_spu_trampoline_fn=NULL;
        spu_0031_at_00ECEC00_spu_func_0000EB10(a);finish_division(a);
        steps=0;g_spu_trampoline_fn=NULL;
        spu_0031_at_00ECEC00_spu_func_0000EB10(b);
        require(haze_job13_native_divide_prefix(b),"unexpected native rejection");
        g_spu_trampoline_fn=NULL;
        spu_0031_at_00ECEC00_spu_func_0000EB24(b);finish_division(b);
        equal_bytes(a->gpr,b->gpr,sizeof a->gpr,"division final GPR");
        equal_bytes(a->ls,b->ls,SPU_LS_SIZE,"division final LS");
        require(a->pc==b->pc,"division continuation");
    }
    /* Guard failure must leave the entire context unchanged. */
    spu_0031_at_00ECEC00_spu_func_0000EB10(b);
    b->ls[0xf2e0]^=1;
    memcpy(a,b,sizeof *a);
    require(!haze_job13_native_divide_prefix(b),"changed carry table accepted");
    equal_bytes(a,b,sizeof *a,"rejected context mutated");
    puts("PASS 10000 original/native integer division cases, all registers/local store/continuation; immutable rejection");
    _aligned_free(a);_aligned_free(b);free(ls);return 0;
}
