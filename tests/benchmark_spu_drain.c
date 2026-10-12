/* Both actual functions in one process on one allowed logical processor.
 * Baseline object is the unchanged hook extracted from historical SDK source
 * by scripts/prepare_spu_drain_baseline.py. This measures local hook cost;
 * the shared stack-reset spy deliberately does not execute a real scheduler. */
#include <assert.h>
#include <windows.h>
#include <stdio.h>
#include "../external/ps3recomp/runtime/spu/spu_drain.c"
static volatile LONG stack_checks;
void spu_halt(spu_context* ctx) { (void)ctx; assert(0); }
__declspec(noinline) void spu_check_stack_reset(spu_context* ctx, void (*fn)(spu_context*))
{ (void)ctx; (void)fn; ++stack_checks; }
void yz_lockstep_tick(spu_context* ctx) { (void)ctx; assert(0); }
void spu_restart_dispatch(spu_context* ctx) { (void)ctx; assert(0); }
void spu_indirect_branch(spu_context* ctx) { (void)ctx; assert(0); }
void (*spu_lookup(uint32_t pc,int image))(spu_context*)
{ (void)pc; (void)image; assert(0); return NULL; }
void baseline_spu_task_launch_check(spu_context*,void*);
unsigned baseline_spu_recent_pcs(uint32_t*,unsigned);
typedef void (*hook)(spu_context*,void*);
typedef unsigned (*history_fn)(uint32_t*,unsigned);
static __declspec(noinline) void measure(hook call,history_fn recent,const char* name,spu_context* ctx)
{
    const unsigned count=5000000;
    ctx->steps=0; stack_checks=0;
    ULONGLONG begin,end; LARGE_INTEGER start,finish,frequency;
    assert(QueryPerformanceFrequency(&frequency));
    assert(QueryThreadCycleTime(GetCurrentThread(),&begin));
    assert(QueryPerformanceCounter(&start));
    for(unsigned n=0;n<count;++n) {
        ctx->pc=0x3000+(n & 0xFFF); call(ctx,NULL);
    }
    assert(QueryPerformanceCounter(&finish));
    assert(QueryThreadCycleTime(GetCurrentThread(),&end));
    assert(ctx->steps==count && stack_checks==count);
    uint32_t history[32]; assert(recent(history,32)==32);
    for(unsigned n=0;n<32;++n) assert(history[n]==0x3000+((count-32+n)&0xFFF));
    printf("{\"mode\":\"%s\",\"calls\":%u,\"cpu\":%lu,\"cycles\":%llu,\"cycles_per_call\":%.3f,\"elapsed_ms\":%.3f}\n",
        name,count,GetCurrentProcessorNumber(),end-begin,(double)(end-begin)/count,
        (double)(finish.QuadPart-start.QuadPart)*1000/frequency.QuadPart);
}
int main(void)
{
    const char* variables[]={"SPU_NO_LS0_END","SPU_EXITTRACE","SPU_STEPTRACE","SPU_STEPTRACE_FROM","SPU_STEPTRACE_REGS","SPU_JOBDRAIN"};
    for(unsigned n=0;n<sizeof(variables)/sizeof(variables[0]);++n) _putenv_s(variables[n],"");
    DWORD_PTR allowed,system;
    assert(GetProcessAffinityMask(GetCurrentProcess(),&allowed,&system) && allowed);
    DWORD_PTR selected=allowed & (~allowed+1);
    DWORD_PTR previous=SetThreadAffinityMask(GetCurrentThread(),selected); assert(previous);
    spu_context* ctx=calloc(1,sizeof(*ctx)); assert(ctx); ctx->image_id=40;
    const unsigned order[]={0,1,1,0,1,0,0,1};
    for(unsigned round=0;round<2;++round) for(unsigned n=0;n<8;++n) {
        if(order[n]) measure(spu_task_launch_check,spu_recent_pcs,"outlined",ctx);
        else measure(baseline_spu_task_launch_check,baseline_spu_recent_pcs,"baseline",ctx);
    }
    assert(SetThreadAffinityMask(GetCurrentThread(),previous));
    free(ctx); return 0;
}
