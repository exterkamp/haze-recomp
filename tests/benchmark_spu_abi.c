/* Standalone diagnostic, not an FPS benchmark. A no-op SPU job runs twice on
 * each fresh host thread to isolate first-use ABI/stack overhead. No game data.
 * Link only this source; the stubs intentionally exclude runtime execution. */
#define YZ_PERF_PROFILE 1
#include "spu_lifted_job.h"
#include <windows.h>
#include <assert.h>

uint8_t* vm_base;
void (*g_spu_out_mbox_hook)(uint32_t,uint32_t,int,uint32_t);
void spu_thread_publish_ctx(uint32_t tid, void* ctx) { (void)tid; (void)ctx; }
void spu_coh_unregister(spu_context* ctx) { (void)ctx; }
void spu_mfc_release(spu_context* ctx) { (void)ctx; }
int spu_run_with_halt(void (*entry)(spu_context*),spu_context* ctx) {
    entry(ctx); return 0;
}
static volatile unsigned checksum;
static void entry(spu_context* ctx) { checksum += ctx->gpr[1]._u32[0]; }

typedef struct Sample { uint64_t outside[2], inside[2]; } Sample;
/* Keep stack probing inside the timed call, rather than letting the compiler
 * fold the context allocation into the worker's untimed entry prologue. */
__declspec(noinline) static int run_abi(spu_abi_profile* profile) {
    return spu_run_lifted_job_abi_profiled(entry,NULL,0,40,1,NULL,NULL,profile);
}
static DWORD WINAPI worker(void* arg) {
    Sample* s=(Sample*)arg;
    ULONG guarantee=256*1024; SetThreadStackGuarantee(&guarantee);
    for (unsigned i=0;i<2;i++) {
        ULONG64 begin=0,end=0; spu_abi_profile profile={0};
        QueryThreadCycleTime(GetCurrentThread(),&begin);
        assert(run_abi(&profile)==0);
        QueryThreadCycleTime(GetCurrentThread(),&end);
        s->outside[i]=end-begin;
        s->inside[i]=profile.prepare_cycles+profile.execute_cycles+profile.finish_cycles;
    }
    return 0;
}
int main(void) {
    const unsigned count=1000;
    uint64_t outside[2]={0},inside[2]={0};
    for (unsigned n=0;n<count;n++) {
        Sample s={0};
        HANDLE th=CreateThread(NULL,256u<<20,worker,&s,STACK_SIZE_PARAM_IS_A_RESERVATION,NULL);
        assert(th); assert(WaitForSingleObject(th,INFINITE)==WAIT_OBJECT_0);
        CloseHandle(th);
        for (unsigned i=0;i<2;i++) { outside[i]+=s.outside[i]; inside[i]+=s.inside[i]; }
    }
    for (unsigned i=0;i<2;i++)
        printf("pass=%u samples=%u mean_call_cycles=%.1f mean_inside_cycles=%.1f mean_gap_cycles=%.1f\n",
            i,count,(double)outside[i]/count,(double)inside[i]/count,
            (double)(outside[i]-inside[i])/count);
    return 0;
}
