// The original constant-upload function is extracted from the private lift.
// Command emission is intact; this harness provides sufficient FIFO capacity.
// Unexpected refill, indirect targets or trampoline fallthrough fail closed.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#ifdef HAZE_TEST_VERTEX_PARAMETER_SDK
#include "haze_vertex_parameter_consumer.h"
extern "C" {
std::uint32_t ppu_hle_inject_base = 0;
void ps3_ms(const char*) { assert(false && "unexpected SDK milestone"); }
void vm_write32(std::uint64_t, std::uint32_t);
}
static rsx_dispatch dispatch_original, dispatch_native;
static rsx_state state_original, state_native;
#endif
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
#include "haze_vertex_parameter_packet.h"
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
#ifdef HAZE_TEST_VERTEX_PARAMETER_SDK
  std::memset(&dispatch_original,0xa5,sizeof dispatch_original);
  std::memset(&state_original,0xa5,sizeof state_original);
  state_original.vertex_constants_dirty=count%2;
  state_original.vertex_constants_lo=210;state_original.vertex_constants_hi=400;
  std::memcpy(&dispatch_native,&dispatch_original,sizeof dispatch_original);
  std::memcpy(&state_native,&state_original,sizeof state_original);
#endif
  const auto end=vm_read32(0x11008);unsigned packets=0;
  for(unsigned at=0x300000;at<end;) {
   const unsigned h=vm_read32(at);at+=4;const auto n=(h>>18)&2047,method=h&0x1ffc;assert((h>>29)==0);
   assert(at+n*4<=end);++packets;
   for(unsigned i=0;i<n;++i) {
    unsigned m=method+i*4,v=vm_read32(at);at+=4;
#ifdef HAZE_TEST_VERTEX_PARAMETER_SDK
    rsx_dispatch_method(&dispatch_original,m,v);
    assert(rsx_process_method(&state_original,m,v)==0);
#endif
    if(m==0x1efc)ol=v;else {assert(m>=0x1f00 && m<0x1f80);unsigned w=(m-0x1f00)/4;ow[w]=v;assert(ol+w/4<512);original[ol+w/4][w%4]=v;}
   }
  }

  assert(haze::vertex_parameters::apply(plan,memory.data()+0x400000,plan.source_bytes,native,nw,nl));
  assert(std::memcmp(original,native,sizeof native)==0);assert(std::memcmp(ow,nw,sizeof nw)==0);assert(ol==nl);++tests;
  // Encode an owned snapshot, mutate the original source, then consume. This
  // tests byte ownership independently of the unimplemented live FIFO bridge.
  std::vector<std::uint8_t> packet(haze::vertex_parameters::packet_bytes(plan)+16,0xea);
  assert(haze::vertex_parameters::encode(plan,memory.data()+0x400000,plan.source_bytes,packet.data(),packet.size()));
  std::memset(memory.data()+0x400000,0x37,plan.source_bytes);
  haze::vertex_parameters::layout decoded{};const std::uint8_t* payload=nullptr;std::size_t used=0;
  assert(haze::vertex_parameters::decode(packet.data(),packet.size(),decoded,payload,used));
  assert(used==packet.size()-16);
  for(std::size_t i=used;i<packet.size();++i) assert(packet[i]==0xea);
#ifdef HAZE_TEST_VERTEX_PARAMETER_SDK
  assert(haze::vertex_parameters::apply_dispatch(dispatch_native,decoded,payload,decoded.source_bytes));
  assert(haze::vertex_parameters::apply_state(state_native,decoded,payload,decoded.source_bytes));
  assert(std::memcmp(&dispatch_original,&dispatch_native,sizeof dispatch_native)==0);
  assert(std::memcmp(&state_original,&state_native,sizeof state_native)==0);
  // Partial legacy writes after the native operation must observe its final
  // LOAD/window state. Follow with another LOAD and an unrelated register.
  for(auto mv : {std::vector<unsigned>{0x1f04,0x7fc12345}, {0x1f7c,0x80000000},
                {0x1efc,511}, {0x1f00,0x12345678}, {NV4097_SET_VERTEX_ATTRIB_INPUT_MASK,0x42}}) {
    rsx_dispatch_method(&dispatch_original,mv[0],mv[1]);
    rsx_dispatch_method(&dispatch_native,mv[0],mv[1]);
    assert(rsx_process_method(&state_original,mv[0],mv[1])==0);
    assert(rsx_process_method(&state_native,mv[0],mv[1])==0);
  }
  assert(std::memcmp(&dispatch_original,&dispatch_native,sizeof dispatch_native)==0);
  assert(std::memcmp(&state_original,&state_native,sizeof state_native)==0);
#endif
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
 // Rejection is transactional: no output or destination changes.
 std::uint8_t packet[24];std::memset(packet,0xa7,sizeof packet);
 assert(!haze::vertex_parameters::encode(invalid,source,3,packet,sizeof packet));
 for(auto b:packet)assert(b==0xa7);
 assert(haze::vertex_parameters::encode(invalid,source,4,packet,sizeof packet));
 const std::uint8_t* payload=source;std::size_t used=999;
 for(unsigned n=0;n<sizeof packet;++n) {
  auto untouched=saved;
  assert(!haze::vertex_parameters::decode(packet,n,untouched,payload,used));
  assert(std::memcmp(&untouched,&saved,sizeof saved)==0 && payload==source && used==999);
 }
 for(unsigned field=0;field<5;++field) {
  std::uint8_t bad[24];std::memcpy(bad,packet,sizeof bad);
  haze::vertex_parameters::write_be(bad+field*4,UINT32_MAX);
  auto untouched=saved;
  assert(!haze::vertex_parameters::decode(bad,sizeof bad,untouched,payload,used));
  assert(std::memcmp(&untouched,&saved,sizeof saved)==0 && payload==source && used==999);
 }
 std::printf("PASS %u full original-function constant-state/window comparisons\n",tests);
#ifdef HAZE_TEST_VERTEX_PARAMETER_SDK
 std::printf("PASS %u actual SDK full-state comparisons, dirty ranges, coverage counters, owned source mutation and mixed legacy continuation\n",tests);
#endif
}

