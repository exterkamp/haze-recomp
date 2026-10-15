#include "haze_indexed_draw_capture.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
std::uint8_t* vm_base=nullptr;
int ppu_native_memory_range(std::uint32_t a,std::uint32_t n) {
    return n && a>=0x10000 && std::uint64_t(a)+n<=0x40000;
}
}
void put(std::uint32_t a,std::uint32_t v) {for(int i=0;i<4;++i) vm_base[a+i]=std::uint8_t(v>>(24-i*8));}
void setup() {put(0x11000,0x20000);put(0x11004,0x30000);put(0x11008,0x20000);}
void stream() {
    const std::uint32_t words[]={0x41714,0,0x8181c,0x12340,17,0x41808,5,0x41824,0x02000000,0x41808,0};
    for(unsigned i=0;i<11;++i) put(0x20000+4*i,words[i]);
    put(0x11008,0x2002c);
}
int main() {
    std::vector<std::uint8_t> memory(0x40000,0xcd);vm_base=memory.data();
    for(unsigned variant=0;variant<6;++variant) {
        setup();
        haze_indexed_capture::scope s(1,0xe94ee0,0x7e4140);
        // A preceding capacity callback is recorded separately and does not
        // invalidate a later complete, contiguous emission.
        s.callback(0x20000100,7);
        s.begin(0x731390,variant==1?0x3ffff:0x11000,0x20000,0x12340,1,5,3,0);
        stream();
        if(variant==2) s.callback(0x20000100,3);
        if(variant==3) put(0x11004,0x2ff00);
        if(variant==4) put(0x11008,0x22000);
        auto before=memory;
        if(variant!=5) s.finish();
        assert(memory==before);
    }
}
