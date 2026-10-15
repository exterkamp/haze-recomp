// The original constant-upload function is extracted from the private lift.
// Command emission is intact; this harness provides sufficient FIFO capacity.
// Unexpected refill, indirect targets or trampoline fallthrough fail closed.
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

std::uint8_t vm_read8(std::uint64_t a) {return std::uint8_t(read(unsigned(a),1));}
std::uint16_t vm_read16(std::uint64_t a) {return std::uint16_t(read(unsigned(a),2));}
void vm_write8(std::uint64_t a,std::uint8_t v) {write(unsigned(a),v,1);}
void vm_write16(std::uint64_t a,std::uint16_t v) {write(unsigned(a),v,2);}
void ps3_indirect_call(ppu_context*) {assert(false && "unexpected refill or dispatch");}
#define DRAIN_TRAMPOLINE(ctx) ((void)0)
void (*g_trampoline_fn)(void*);
void func_0072F038(ppu_context*) {assert(false && "unexpected fallthrough");}
#include "vertex_parameter_original.inc"
#include "haze_vertex_parameter_batch.h"
int main() {
 const unsigned jump[]={0x2b0,0x2ec,0xb20,0xa3c,0x7d4,0x2ec,0xb20,0xa3c,0x7d4,0x6ec,0x604,0x44,0x398,0x6ec,0x604,0x44,0x398};
 const unsigned counts[]={0,1,2,7,8,9,16,31,32,127,128,511,512};unsigned tests=0;
 for(unsigned type=0;type<=16;++type) for(unsigned count:counts) for(unsigned first : {0u,20u,511u}) {
  haze::vertex_parameters::layout plan;
  if(!haze::vertex_parameters::plan(type,count,first,plan)) continue;
  std::fill(memory.begin(),memory.end(),0xcd);
  ppu_context c{};c.gpr[1]=0x900000;c.gpr[2]=0xe94ee0;c.lr=0x6737fc;c.gpr[3]=0x14000;c.gpr[4]=0x13000;
  vm_write32(0xe94ee0-0x7c00,0x200000);vm_write32(0x200000-0x7ffc,0x12000);vm_write32(0x12000,0x11000);
  vm_write32(0x200000-0x7fa0,0x18000);
  for(unsigned i=0;i<17;++i)vm_write32(0x18000+i*4,0x72e420+jump[i]-0x18000);
  vm_write32(0x11000,0x300000);vm_write32(0x11004,0x380000);vm_write32(0x11008,0x300000);
  vm_write32(0x13004,0x400000);vm_write16(0x13010,first);vm_write16(0x13014,count);vm_write8(0x13019,type);vm_write8(0x1301b,0xff);
  for(unsigned i=0;i<2048;++i) {
   const unsigned edges[]={0u,0x80000000u,0x7fc12345u,0x7f800000u,0xff800000u,0x7f812345u,1u,0xffffffffu};
   vm_write32(0x400000+i*4, i%3 ? (0x12345678u+i*0x671239u)^0xf03478u : edges[(i/3)%8]);
  }
  func_0072E398(&c);assert(c.gpr[1]==0x900000 && c.lr==0x6737fc && vm_read8(0x1301b)==0xfd);
  std::uint32_t original[512][4],native[512][4],ow[32],nw[32],ol=0xcccccccc,nl=ol;
  std::memset(original,0xab,sizeof original);std::memcpy(native,original,sizeof native);
  std::memset(ow,0xab,sizeof ow);std::memcpy(nw,ow,sizeof nw);
  const auto end=vm_read32(0x11008);unsigned packets=0;
  for(unsigned at=0x300000;at<end;) {
   const unsigned h=vm_read32(at);at+=4;const auto n=(h>>18)&2047,method=h&0x1ffc;assert((h>>29)==0);
   assert(at+n*4<=end);++packets;
   for(unsigned i=0;i<n;++i) {
    unsigned m=method+i*4,v=vm_read32(at);at+=4;
    if(m==0x1efc)ol=v;else {assert(m>=0x1f00 && m<0x1f80);unsigned w=(m-0x1f00)/4;ow[w]=v;assert(ol+w/4<512);original[ol+w/4][w%4]=v;}
   }
  }

  assert(haze::vertex_parameters::apply(plan,memory.data()+0x400000,plan.source_bytes,native,nw,nl));
  assert(std::memcmp(original,native,sizeof native)==0);assert(std::memcmp(ow,nw,sizeof nw)==0);assert(ol==nl);++tests;
 }
 haze::vertex_parameters::layout invalid{123,1,1,4}, saved=invalid;
 for(auto args : {std::vector<int>{17,1,0},{1,-1,0},{1,1,-1},{1,1,512},{16,129,0}}) {
  assert(!haze::vertex_parameters::plan(args[0],args[1],args[2],invalid));
  assert(std::memcmp(&saved,&invalid,sizeof saved)==0);
 }
 std::uint32_t constants[512][4]{},window[32]{},load=123;
 std::uint8_t source[4]{};
 assert(!haze::vertex_parameters::apply(invalid,source,3,constants,window,load));
 assert(!haze::vertex_parameters::apply(invalid,nullptr,4,constants,window,load));
 assert(load==123 && constants[123][0]==0 && window[0]==0);
 std::printf("PASS %u full original-function constant-state/window comparisons\n",tests);
}

