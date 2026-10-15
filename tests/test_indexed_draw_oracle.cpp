// The original function is extracted from the user's private lift by the
// runner. Only external setup helpers are stubbed; command emission is intact.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
struct ppu_context {std::uint64_t gpr[32]{},lr=0;std::uint32_t cr=0,ctr=0;};
std::vector<std::uint8_t> memory(0x1000000);
std::uint64_t read(unsigned a,unsigned n) {
    assert(std::uint64_t(a)+n<=memory.size());std::uint64_t v=0;
    for(unsigned i=0;i<n;++i) v=(v<<8)|memory[a+i];return v;
}
void write(unsigned a,std::uint64_t v,unsigned n) {
    assert(std::uint64_t(a)+n<=memory.size());
    for(unsigned i=0;i<n;++i) memory[a+i]=std::uint8_t(v>>(8*(n-1-i)));
}
std::uint32_t vm_read32(std::uint64_t a) {assert(a<=UINT32_MAX);return std::uint32_t(read(unsigned(a),4));}
std::uint64_t vm_read64(std::uint64_t a) {assert(a<=UINT32_MAX);return read(unsigned(a),8);}
void vm_write32(std::uint64_t a,std::uint32_t v) {assert(a<=UINT32_MAX);write(unsigned(a),v,4);}
void vm_write64(std::uint64_t a,std::uint64_t v) {assert(a<=UINT32_MAX);write(unsigned(a),v,8);}
std::uint64_t rotate(std::uint64_t v,unsigned n) {n&=63;return n?(v<<n)|(v>>(64-n)):v;}
std::uint64_t ppc_rldicl(std::uint64_t v,unsigned n,unsigned mb) {return rotate(v,n)&(UINT64_MAX>>mb);}
std::uint64_t ppc_rldicr(std::uint64_t v,unsigned n,unsigned me) {return rotate(v,n)&(UINT64_MAX<<(63-me));}
std::uint32_t ppc_rlwinm(std::uint32_t v,unsigned n,unsigned mb,unsigned me) {
    n&=31;const auto r=n?(v<<n)|(v>>(32-n)):v;std::uint32_t mask=0;
    for(unsigned bit=0;bit<32;++bit) if(mb<=me?(bit>=mb&&bit<=me):(bit>=mb||bit<=me)) mask|=std::uint32_t(1)<<(31-bit);
    return r&mask;
}
unsigned resolved;
void func_00730440(ppu_context* c) {c->gpr[3]=resolved;}
void func_006F0C50(ppu_context*) {}
void ps3_indirect_call(ppu_context*) {assert(false && "unexpected command buffer refill");}
#define DRAIN_TRAMPOLINE(ctx) ((void)0)
#include "indexed_draw_original.inc"

int main(int argc,char** argv) {
    assert(argc==2);FILE* out=std::fopen(argv[1],"wb");assert(out);
    const unsigned sizes[]={1,2,3,31,32,63,64,65,255,256,257,511,512,513,0x7ff00,0x7ff01};
    const unsigned alignments[]={0,2,4,126};
    for(unsigned mode=0;mode<2;++mode) for(unsigned type=0;type<2;++type)
    for(unsigned n:sizes) for(unsigned a:alignments) {
        std::fill(memory.begin(),memory.end(),0xcd);
        ppu_context ctx{};ctx.gpr[1]=0x900000;ctx.gpr[2]=0xe94ee0;ctx.lr=0x7e4140;
        ctx.gpr[3]=3;ctx.gpr[4]=n;ctx.gpr[5]=1;
        resolved=mode?0x10000:0;
        ctx.gpr[6]=mode?a:0x500000+a;
        vm_write32(0xe94ee0-0x7bfc,0x200000);
        vm_write32(0x200000-0x7fd8,0x15000);vm_write32(0x15000,0x500000);
        vm_write32(0x200000-0x7fd4,0x16000);vm_write32(0x16000,0x500000);
        vm_write32(0x200000-0x7fc4,0x12000);vm_write32(0x12000,0x11000);
        vm_write32(0x200000-0x7f0c,0x13000);vm_write32(0x13000+12,5);
        vm_write32(0x200000-0x7f08,0x14000);vm_write32(0x14000+4,type);
        vm_write32(0x11000,0x300000);vm_write32(0x11004,0x400000);vm_write32(0x11008,0x300000);
        func_007312B0(&ctx);
        assert(ctx.gpr[1]==0x900000 && ctx.lr==0x7e4140 && ctx.gpr[2]==0xe94ee0);
        unsigned end=vm_read32(0x11008);assert(end>0x300000 && end<=0x400000);
        unsigned values[]={mode,type,n,resolved+a,end-0x300000};
        assert(std::fwrite(values,sizeof(values),1,out)==1);
        assert(std::fwrite(memory.data()+0x300000,1,end-0x300000,out)==end-0x300000);
    }
    assert(std::fclose(out)==0);
}
