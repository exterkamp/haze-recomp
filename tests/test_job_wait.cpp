// Synthetic caller of the real native boundary; no guest data or timing claims.
#include "ppu_recomp.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <cstdio>
void haze_job_wait_call(ppu_context*);
bool haze_job_wait_usleep(ppu_context*);
static bool handled, throw_inside;
static unsigned duration=330;
void func_0068EFC0(ppu_context* ctx) {
    ctx->lr=0x0068EFF0;ctx->gpr[11]=0x8D;ctx->gpr[3]=duration;
    handled=haze_job_wait_usleep(ctx);
    if(throw_inside)throw std::runtime_error("synthetic unwind");
}
static ppu_context caller() {
    ppu_context ctx={};ctx.thread_id=1;ctx.gpr[2]=0xE94EE0;
    ctx.lr=0x00682574;ctx.gpr[31]=7;return ctx;
}
int main() {
    _putenv_s("HAZE_NATIVE_JOB_WAIT","1");
    _putenv_s("HAZE_JOB_WAIT_FILE","");
    for(unsigned i=0;i<130;++i){auto ctx=caller();haze_job_wait_call(&ctx);assert(handled&&ctx.gpr[3]==0);}
    for(unsigned field=0;field<5;++field){auto ctx=caller();duration=330;
        if(field==0)ctx.thread_id=2;
        if(field==1)ctx.gpr[2]++;
        if(field==2)ctx.lr++;
        if(field==3)ctx.gpr[31]=512;
        if(field==4)duration=331;
        haze_job_wait_call(&ctx);assert(!handled&&ctx.gpr[3]==duration);
    }
    duration=330;auto ctx=caller();throw_inside=true;
    try{haze_job_wait_call(&ctx);assert(false);}catch(const std::runtime_error&){}
    // An exception must not leak authorization to an unrelated delay.
    ctx.lr=0x0068EFF0;ctx.gpr[11]=0x8D;ctx.gpr[3]=330;
    auto before=ctx;assert(!haze_job_wait_usleep(&ctx));assert(!memcmp(&ctx,&before,sizeof ctx));
    puts("PASS: scoped job wait, exact thread/TOC/call/job/duration guards, result and unwind");
}
