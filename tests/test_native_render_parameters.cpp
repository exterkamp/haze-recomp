// Differential test against the actual private generated function. Callback
// substitutes record arguments/order, clobber volatile state and mutate tables.
#include "ppu_recomp.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

static std::vector<uint8_t> ram(0x20000);
static unsigned accesses;
static uint32_t mutation;
static bool trampoline_pending;
static std::vector<std::array<uint64_t, 4>> events;
extern "C" uint8_t vm_read8(uint64_t a) { ++accesses; return ram.at(uint32_t(a)); }
extern "C" uint16_t vm_read16(uint64_t a) { return uint16_t(vm_read8(a))<<8 | vm_read8(a+1); }
extern "C" uint32_t vm_read32(uint64_t a) { return uint32_t(vm_read16(a))<<16 | vm_read16(a+2); }
extern "C" uint64_t vm_read64(uint64_t a) { return uint64_t(vm_read32(a))<<32 | vm_read32(a+4); }
extern "C" void vm_write8(uint64_t a, uint8_t v) { ++accesses; ram.at(uint32_t(a))=v; }
extern "C" void vm_write32(uint64_t a, uint32_t v) { for(unsigned i=0;i<4;++i)vm_write8(a+i,uint8_t(v>>(24-i*8))); }
extern "C" void vm_write64(uint64_t a, uint64_t v) { vm_write32(a,uint32_t(v>>32)); vm_write32(a+4,uint32_t(v)); }
static uint64_t ppc_rldicl(uint64_t v,unsigned s,unsigned m) { assert(!s); return v & (UINT64_MAX>>m); }
static uint32_t ppc_rlwinm(uint32_t v,unsigned s,unsigned b,unsigned e) {
    v = s ? (v<<s)|(v>>(32-s)) : v;
    assert(b<=e); return v & ((UINT32_MAX>>b)&(UINT32_MAX<<(31-e)));
}
static void callback(ppu_context* c,unsigned id) {
    events.push_back({id,c->gpr[3],c->gpr[4],id==2?c->gpr[5]:0});
    assert(c->gpr[1]==0x17F40);
    assert(c->lr==(id==1?0x6737FCu:id==2?0x67387Cu:id==3?0x6738F8u:0x6739C8u));
    if(id==1 && (mutation&2)) {
        vm_write32(0x102C,0x6100); vm_write32(0x103C,0x4200);
        vm_write8(0x100F,2); vm_write32(0x1008,0x2800);
    }
    if(id==2)vm_write8(c->gpr[4]+0x1B,vm_read8(c->gpr[4]+0x1B)&~1u);
    if(id==3 && mutation) {
        // Both counts and table base change between parameter/sampler passes.
        vm_write8(0x100D,2); vm_write8(0x100F,1); vm_write32(0x1008,0x2800);
    }
    if(id==4 && mutation) vm_write32(0x1038,0x4900); // reload sampler units per entry
    for(unsigned r=0;r<=12;++r)if(r!=1 && r!=2)c->gpr[r]=0xB000+r;
    c->cr=(c->cr&0x00FFF000)|0xA5000567; c->ctr=0xFE; c->lr=0xDEADBEEF;
    trampoline_pending=true;
}
void func_0072E398(ppu_context* c) { callback(c,1); }
void func_0072A738(ppu_context* c) { callback(c,2); }
void func_007286A0(ppu_context* c) { callback(c,3); }
void func_00725228(ppu_context* c) { callback(c,4); }
static void drain(ppu_context*) { if(trampoline_pending){ events.push_back({5,0,0,0}); trampoline_pending=false; } }
#define DRAIN_TRAMPOLINE(c) drain(c)
#include "render_parameters_reference.inc"
#include "haze_native_render_parameters.h"

static ppu_context prepare(unsigned n,unsigned flags,unsigned mode) {
    ram.assign(ram.size(),0xCC); events.clear();trampoline_pending=false;
    ppu_context c{};for(unsigned i=0;i<32;++i)c.gpr[i]=0xAA000000+i;
    c.gpr[1]=0x18000;c.gpr[2]=0xE94EE0;c.gpr[3]=0x1020;
    c.gpr[5]=mode&1?0x101:0x100;c.thread_id=1;c.lr=0x6741AC;c.cr=0x87654321;
    vm_write32(0x1028,mode&2?0:0x5000);vm_write32(0x102C,mode&4?0:0x6000);
    vm_write32(0x1038,0x4800);vm_write32(0x103C,0x4000);
    vm_write32(0x1000,0x2000);vm_write8(0x1006,0);vm_write8(0x1007,uint8_t(n));
    vm_write32(0x1008,0x2400);
    vm_write8(0x100C,0);vm_write8(0x100D,uint8_t(n));
    vm_write8(0x100E,0);vm_write8(0x100F,uint8_t(n));
    for(unsigned i=0;i<2*n+4;++i) {
        const uint32_t entry=0x3000+0x20*i;
        vm_write32(0x2000+i*4,entry);vm_write32(0x2400+i*4,entry);vm_write32(0x2800+i*4,entry);
        vm_write32(entry,0x7000+i*16);vm_write8(entry+0x1B,uint8_t(flags^(i&3)));
        vm_write32(0x4000+i*4,0x8000+i*16);
        vm_write32(0x4200+i*4,0x9000+i*16);
        vm_write32(0x4800+i*4,i%3==0?UINT32_MAX:i%3==1?15:16);
        vm_write32(0x4900+i*4,i%16);
    }
    if(mode&8)c.gpr[3]=0;
    return c;
}
int main() {
    unsigned cases=0;uint64_t old_accesses=0,new_accesses=0;
    for(unsigned n: {0u,1u,2u,7u,31u})for(unsigned flags=0;flags<4;++flags)
    for(unsigned mode=0;mode<16;++mode)for(mutation=0;mutation<4;++mutation) {
        auto reference=prepare(n,flags,mode),native=reference;
        const auto before=ram;accesses=0;
        reference_render_parameters(&reference);old_accesses+=accesses;
        const auto expected=ram;const auto expected_events=events;
        ram=before;events.clear();accesses=0;
        assert(haze_native_render_parameters(&native));new_accesses+=accesses;
        assert(events==expected_events && !trampoline_pending);
        assert(vm_read64(0x17F40)==0x18000);
        // Guest scratch save slots are intentionally replaced by host locals.
        for(unsigned i=0;i<ram.size();++i)if(i<0x17F40||i>=0x18018)assert(ram[i]==expected[i]);
        assert(native.gpr[1]==reference.gpr[1] && native.gpr[2]==reference.gpr[2]);
        for(unsigned r=14;r<32;++r)assert(native.gpr[r]==reference.gpr[r]);
        assert((native.cr&0x00FFF000)==(reference.cr&0x00FFF000) && native.lr==reference.lr);
        ++cases;
    }
    for(unsigned guard=0;guard<6;++guard) {
        auto c=prepare(1,3,1);
        switch(guard){case 0:c.thread_id=2;break;case 1:++c.gpr[2];break;
        case 2:c.gpr[3]=UINT64_C(0x100001020);break;case 3:c.gpr[1]=0xB0;break;
        case 4:c.gpr[1]=UINT32_MAX;break;case 5:++c.gpr[1];break;}
        const auto old=c;const auto memory=ram;
        assert(!haze_native_render_parameters(&c));assert(!std::memcmp(&c,&old,sizeof c)&&ram==memory&&events.empty());
    }
    std::printf("Native parameter traversal: %u differential cases; byte accessor calls %llu -> %llu; fallback guards passed\n",
        cases,(unsigned long long)old_accesses,(unsigned long long)new_accesses);
}
